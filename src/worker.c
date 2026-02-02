#include <errno.h>
#include <signal.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#include "const.h"
#include "msg.h"
#include "objects.h"
#include "tools.h"

/* =========================================================================
 * Prototypes
 * ========================================================================= */

void work_with_pause(
    simctx_t *ctx, station *st, size_t variance, msg_t *response, worker_t *self
);
void work_shift(
    simctx_t *ctx, station *st, size_t variance, msg_t *response, worker_t *self
);
void serve_client(
    const worker_t *self,
    simctx_t       *ctx,
    station        *st,
    msg_t          *response,
    const double    variance
);
static inline void _serve_food(
    simctx_t       *ctx,
    const worker_t *self,
    station        *st,
    msg_t          *response,
    size_t          time
);
static inline void _serve_checkout(simctx_t *ctx, station *st, msg_t *response);
static inline station *get_station(const shmid_t shmid, const loc_t role);

/* =========================================================================
 * Functions
 * ========================================================================= */

/**
 * @brief Main entry point for the worker process.
 * * It initializes signal handling, parses IPC identifiers from arguments,
 * and enters the main simulation loop where it waits for the start of a day.
 * * @param argc Argument count.
 * @param argv Arguments: {exec_name, ctx_shmid, sts_shmid, worker_idx, role}.
 * @return int Exit status.
 */
int
main(int argc, char **argv) {
    /* Ignore SIGINT: the worker is managed by the master process via SIGUSR1 or
     * SHM flags. */
    signal(SIGINT, SIG_IGN);

    if (argc != 5)
        panic("ERROR: Invalid worker arguments for pid: %d", getpid());

    /* Parse IPC identifiers and worker metadata from command line arguments */
    const shmid_t ctx_id = atos(argv[1]);
    const shmid_t sts_id = atos(argv[2]);
    const size_t  idx    = atos(argv[3]);
    const loc_t   role   = atos(argv[4]);

    /* Setup SIGUSR1 handler for custom synchronization/interruptions */
    struct sigaction sa;
    sa.sa_handler = handle_signal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGUSR1, &sa, NULL);

    /* Main simulation loop: handles multiple working days */
    while (true) {
        simctx_t *ctx = get_ctx(ctx_id);

        zprintf(ctx->sem[out], "WORKER: WAITING FOR DAY INITIALIZATION\n");

        /* Wait for the "wall" semaphore: the master releases this when the day
         * starts */
        sem_wait(ctx->sem[wall]);

        /* Check if simulation was terminated while waiting */
        if (!ctx->is_sim_running)
            break;

        station  *st  = get_station(sts_id, role);
        worker_t *wks = get_workers(st->wk_data.shmid);

        const size_t queue    = ctx->id_msg_q[role];
        const size_t variance = var_srvc[role];
        msg_t        response;

        /* Register self in the shared memory worker array */
        sem_wait(st->sem);
        wks[idx] = (worker_t){
            .pid        = getpid(),
            .role       = role,
            .queue      = queue,
            .paused     = true,
            .nof_pause  = 0,
            .pause_time = 0,
        };
        worker_t *self = &wks[idx];
        sem_signal(st->sem);

        /* Enter the active working cycle for the current day */
        work_with_pause(ctx, st, variance, &response, self);

        /* Wait for the master to signal the end of the day */
        sem_wait(ctx->sem[wk_end]);
        if (!ctx->is_sim_running) {
            zprintf(
                ctx->sem[out], "WORKER %d: Day finished, exiting.\n", getpid()
            );
            break;
        }
    }

    return 0;
}

/**
 * @brief Manages the worker's cycle between being active at a station and
 * taking breaks.
 * * @param ctx Global simulation context.
 * @param st Station where the worker is assigned.
 * @param variance Service time variance for the specific role.
 * @param response Buffer for message passing.
 * @param self Pointer to the worker's own data in shared memory.
 */
void
work_with_pause(
    simctx_t *ctx, station *st, size_t variance, msg_t *response, worker_t *self
) {
    while (ctx->is_sim_running && ctx->is_day_running) {
        zprintf(
            ctx->sem[out], "WORKER: id %d, role %d, WAITING FOR SERVICE SLOT\n",
            self->pid, self->role
        );

        /* ACQUIRING STATION SLOT
         * The worker must wait for a physical space (semaphore) at the station.
         * The loop handles interruptions (e.g., signals) to prevent premature
         * exiting. */
        struct timespec t_start, t_end;
        clock_gettime(CLOCK_REALTIME, &t_start);

        int res;
        do {
            res = sem_wait(st->wk_data.sem);
        } while (res == -1 && errno == EINTR);

        /* Track time spent waiting for a slot as 'pause_time' (idle time) */
        clock_gettime(CLOCK_REALTIME, &t_end);
        const long wait_ns = (t_end.tv_sec - t_start.tv_sec) * TO_NANOSEC +
                             (t_end.tv_nsec - t_start.tv_nsec);
        self->pause_time += (size_t)wait_ns;

        /* START ACTIVE SHIFT */
        self->paused = false;
        work_shift(ctx, st, variance, response, self);

        /* RELEASE STATION SLOT (End of Shift / Taking a Break) */
        sem_signal(st->wk_data.sem);

        /* UPDATE GLOBAL STATISTICS */
        sem_wait(ctx->sem[shm]);
        st->stats.total_breaks++;
        sem_signal(ctx->sem[shm]);

        /* Process the break duration as a sleep period */
        self->pause_time += (size_t)ctx->config.pause_duration;
        znsleep((size_t)ctx->config.pause_duration);
    }
}

/**
 * @brief Main processing loop for serving clients.
 * * Listens for messages on the queue, processes them based on role,
 * and occasionally decides to take a break based on probability.
 * * @param ctx Global simulation context.
 * @param st Assigned station.
 * @param variance Service time variance.
 * @param response Buffer for client requests.
 * @param self Pointer to worker's SHM data.
 */
void
work_shift(
    simctx_t *ctx, station *st, size_t variance, msg_t *response, worker_t *self
) {
    while (ctx->is_sim_running && ctx->is_day_running) {
        /* Non-blocking-like receive: wait for messages matching the role's
         * queue */
        const ssize_t res = recv_msg_np(self->queue, -DEFAULT, response);
        if (res == -1 && errno == EINTR)
            continue;

        /* Dispatch based on role. Note: TABLES and EXIT don't have workers. */
        switch (self->role) {
        case COFFEE_BAR:
        case FIRST_COURSE:
        case MAIN_COURSE:
        case CHECKOUT:
            serve_client(self, ctx, st, response, variance);
            break;

        case TABLE:
        case EXIT:
            panic("ERROR: Invalid worker role for pid: %d\n", getpid());
        }

        /* Send response back to the specific client */
        response->mtype = response->client;
        send_msg(self->queue, *response, sizeof(msg_t) - sizeof(long));

        /* RANDOM BREAK LOGIC
         * A worker has a 15% chance to take a break after serving a client,
         * provided they haven't exceeded their daily break limit and
         * they aren't the last worker at the station. */
        if (self->nof_pause < (size_t)ctx->config.nof_pause) {
            if ((rand() % 100) < 15) {
                sem_wait(st->sem);

                int free_seats     = sem_getval(st->wk_data.sem);
                int active_workers = (int)st->wk_data.cap - free_seats;

                if (active_workers > 1) {
                    zprintf(
                        ctx->sem[out],
                        "WORKER %d: Taking a break (Break n.%zu/%d)\n",
                        getpid(), self->nof_pause + 1, ctx->config.nof_pause
                    );
                    self->paused = true;
                    self->nof_pause++;
                    sem_signal(st->sem);
                    break; /* Exit shift loop to return to work_with_pause */
                }
                sem_signal(st->sem);
            }
        }
    }
}

/**
 * @brief High-level service handler.
 * * Calculates service time, simulates the delay, and branches into
 * specific food or checkout logic.
 * * @param self Current worker.
 * @param ctx Global context.
 * @param st Current station.
 * @param response Client message.
 * @param variance Time variance.
 */
void
serve_client(
    const worker_t *self,
    simctx_t       *ctx,
    station        *st,
    msg_t          *response,
    const double    variance
) {
    const size_t avg         = ctx->config.avg_srvc[self->role];
    const size_t actual_time = get_service_time(avg, variance);

    zprintf(
        ctx->sem[out], "WORKER %d: Service time %zu ns\n", getpid(), actual_time
    );

    /* Simulate the time taken to serve the client */
    znsleep(actual_time);

    if (self->role != CHECKOUT) {
        sem_wait(ctx->sem[shm]);
        _serve_food(ctx, self, st, response, actual_time);
        sem_signal(ctx->sem[shm]);
    } else {
        _serve_checkout(ctx, st, response);
    }
}

/**
 * @brief Internal helper to handle food dispensing logic.
 * * Checks inventory, updates stock, and calculates station statistics.
 * * @param ctx Global context.
 * @param self Current worker.
 * @param st Current station.
 * @param response Client message (updated with dish status).
 * @param time Time spent serving.
 */
static inline void
_serve_food(
    simctx_t       *ctx,
    const worker_t *self,
    station        *st,
    msg_t          *response,
    size_t          time
) {
    const size_t             dish_id      = response->dish.id;
    ssize_t                  actual_index = -1;
    struct available_dishes *dishes       = &ctx->avl_dishes[self->role];

    /* Locate the requested dish in the station's available stock */
    for (size_t i = 0; i < dishes->size; i++) {
        if (dishes->data[i].id == dish_id) {
            actual_index = (ssize_t)i;
            break;
        }
    }

    if (actual_index != -1) {
        size_t *quantity_p     = (size_t *)&dishes->data[actual_index].quantity;
        const dish_t dish_info = ctx->menu[self->role].data[actual_index];

        if (*quantity_p > 0) {
            /* Decrement stock (COFFEE_BAR is treated as having infinite supply, what a dream)
             */
            if (self->role != COFFEE_BAR) {
                *quantity_p -= 1;
            }

            /* Update station metrics */
            st->stats.served_dishes++;
            st->stats.worked_time += time;

            response->status = RESPONSE_OK;
            response->dish   = dish_info;
        } else {
            /* Check if the entire category is finished or just this specific
             * dish */
            bool any_dish_left = false;
            for (size_t i = 0; i < dishes->size; i++) {
                if (dishes->data[i].quantity > 0) {
                    any_dish_left = true;
                    break;
                }
            }

            if (any_dish_left) {
                zprintf(
                    ctx->sem[out],
                    "WORKER: Dish %zu finished, but others available.\n",
                    dish_id
                );
                response->status = RESPONSE_DISH_FINISHED;
            } else {
                zprintf(
                    ctx->sem[out], "WORKER: Station %d completely empty!\n",
                    self->role
                );
                response->status = RESPONSE_CATEGORY_FINISHED;
            }
        }
    } else {
        zprintf(
            ctx->sem[out], "ERROR: Dish ID %zu not found in menu %d\n", dish_id,
            self->role
        );
        response->status = ERROR;
    }
}

/**
 * @brief Internal helper to handle payment logic at the checkout.
 * * Checks for "disorder" (system failure) and updates station earnings.
 * * @param ctx Global context.
 * @param st Current station (Checkout).
 * @param response Client message (contains price and ticket info).
 */
static inline void
_serve_checkout(simctx_t *ctx, station *st, msg_t *response) {
    /* Verify access via the disorder semaphore to ensure synchronization during
     * chaos events */
    if (sem_wait(ctx->sem[disorder]) == -1) {
        response->status = ERROR;
        return;
    }
    sem_signal(ctx->sem[disorder]);

    size_t       price    = response->price;
    const size_t discount = (price * DISCOUNT_DISH) / 100;

    /* Apply discount if the client has a ticket */
    if (response->ticket)
        price -= discount;

    /* Update earnings safely */
    sem_wait(st->sem);
    st->stats.earnings += price;
    sem_signal(st->sem);

    response->status = RESPONSE_OK;
}

/**
 * @brief Utility to retrieve a pointer to a specific station from shared
 * memory.
 * * @param shmid Shared memory ID for stations.
 * @param role The role/index of the station.
 * @return station* Pointer to the station structure.
 */
static inline station *
get_station(const shmid_t shmid, const loc_t role) {
    return &(get_stations(shmid))[role];
}
