/* This file contains the main() loop for the client process.
 * The process is launched by main.c with init_client.
 *
 * The client logic follows a state-machine-like flow:
 * Pick dishes -> Request loop (First, Main, Coffee) -> Checkout -> Table ->
 * Exit.
 */

#include "const.h"
#include "msg.h"
#include "objects.h"
#include "tools.h"
#include <signal.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* =============================================================================
 * Prototypes
 * ========================================================================== */

void pick_dishes(ssize_t *menu, const simctx_t *ctx);
void send_request(
    const simctx_t  *ctx,
    client_t        *self,
    msg_t           *response,
    int             *price,
    struct groups_t *group
);
static bool
ask_dish(const simctx_t *ctx, client_t *self, msg_t *msg, msg_t *response);

/* =============================================================================
 * Main logic
 * ========================================================================== */

/**
 * @brief Main entry point for the client process.
 * * Sets up signals, parses command line arguments, and enters the daily
 * simulation loop where the client picks dishes and attempts to be served.
 *
 * @param argc Argument count.
 * @param argv Expected: {exec, ticket_bool, shmid, group_id}.
 * @return int Exit status.
 */
int
main(const int argc, char **argv) {
    /* Ignore CTRL+C as the client lifecycle is managed by the coordinator. */
    signal(SIGINT, SIG_IGN);

    if (argc != 4)
        panic("ERROR: Invalid client arguments for pid: %d", getpid());

    /* Parse simulation parameters from arguments. */
    const bool   ticket = atob(argv[1]);
    const size_t shmid  = atos(argv[2]);
    const size_t grp_id = atos(argv[3]);

    /* Set up SIGUSR1 handler for simulation interrupts. */
    struct sigaction sa;
    sa.sa_handler = handle_signal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGUSR1, &sa, NULL);

    while (true) {
        const simctx_t  *ctx   = get_ctx(shmid);
        struct groups_t *group = (struct groups_t *)&ctx->groups[grp_id];

        zprintf(ctx->sem[out], "CLIENT: Waiting new day\n");

        /* Wait for the start-of-day barrier. */
        sem_wait(ctx->sem[wall]);

        /* Check if simulation was shut down while waiting. */
        if (!ctx->is_sim_running)
            break;

        /* Initialize client state for the current day. */
        client_t self = {
            .pid       = getpid(),
            .ticket    = ticket,
            .loc       = FIRST_COURSE,
            .served    = false,
            .msgq      = ctx->id_msg_q[FIRST_COURSE],
            .wait_time = 0,
            .dishes    = {}
        };

        /* Decide what to eat today. */
        pick_dishes(self.dishes, ctx);

        msg_t response;
        int   price = 0;

        /* Execute the request lifecycle. */
        send_request(ctx, &self, &response, &price, group);

        /* Wait for end-of-day synchronization. */
        sem_wait(ctx->sem[cl_end]);
        if (!ctx->is_sim_running) {
            zprintf(
                ctx->sem[out], "CLIENT %d: Day finished, exiting.\n", getpid()
            );
            break;
        }
    }

    return 0;
}

/**
 * @brief Randomly selects dishes for the client.
 * * The client picks up to one dish for First Course, Main Course, and Coffee.
 * The loop ensures that the client picks at least one substantial course.
 *
 * @param menu Array to store selected dish IDs.
 * @param ctx  Simulation context for menu data.
 */
void
pick_dishes(ssize_t *menu, const simctx_t *ctx) {
    ssize_t rnd;
    ssize_t cur_loc;
    size_t  cnt_nf;

    do {
        cur_loc = -1;
        cnt_nf  = 0;

        /* Pick First Course (-1 represents skipping/not available). */
        rnd = (ssize_t)(rand() % (ctx->menu[FIRST].size + 1) - 1);
        if (rnd != -1) {
            cur_loc     = FIRST_COURSE;
            menu[FIRST] = (ssize_t)ctx->menu[FIRST].data[rnd].id;
        } else {
            cnt_nf++;
            menu[FIRST] = -1;
        }

        /* Pick Main Course. */
        rnd = (ssize_t)(rand() % (ctx->menu[MAIN].size + 1) - 1);
        if (rnd != -1) {
            if (cur_loc == -1)
                cur_loc = MAIN;
            menu[MAIN] = (ssize_t)ctx->menu[MAIN].data[rnd].id;
        } else {
            cnt_nf++;
            menu[MAIN] = -1;
        }

        /* Pick Coffee. */
        rnd = (ssize_t)(rand() % (ctx->menu[COFFEE].size + 1) - 1);
        if (rnd != -1) {
            menu[COFFEE] = (ssize_t)ctx->menu[COFFEE].data[rnd].id;
        } else {
            menu[COFFEE] = -1;
        }

        /* Keep picking until the client has at least one dish planned. */
    } while (menu[FIRST] == -1 && menu[MAIN] == -1);
}

/**
 * @brief Main state machine managing the client's progression through the
 * canteen.
 * * Handles visiting food stations, group synchronization at the checkout,
 * waiting for a table, and finally exiting.
 *
 * @param ctx      Simulation context.
 * @param self     Pointer to current client state.
 * @param response Pointer to message structure for responses.
 * @param price    Accumulated bill.
 * @param group    The group this client belongs to.
 */
void
send_request(
    const simctx_t  *ctx,
    client_t        *self,
    msg_t           *response,
    int             *price,
    struct groups_t *group
) {
    size_t collected = 0;
    while (ctx->is_sim_running && ctx->is_day_running) {

        /* Update message queue based on current location (Station). */
        if (self->loc < NOF_STATIONS)
            self->msgq = ctx->id_msg_q[self->loc];

        /* Prepare the standard request message. */
        msg_t msg = {
            /* mtype contains:
             * - Priority if sent by client.
             * - Client PID if it's a worker response. */
            .mtype = (self->loc == CHECKOUT && self->ticket) ? TICKET : DEFAULT,
            .client = self->pid,
            .dish =
                {self->loc < 3 ? (size_t)self->dishes[self->loc] : 0, "", 0, 0},
            .status = REQUEST_OK,
            .price  = self->loc == CHECKOUT ? (size_t)*price : 0,
            .ticket = self->ticket,
        };

        if (self->loc < 3 && self->dishes[self->loc] != -1) {
            msg.dish.id = (size_t)self->dishes[self->loc];
        }

        switch (self->loc) {
        case FIRST_COURSE:
        case MAIN_COURSE:
        case COFFEE_BAR:
            /* Skip station if no dish was planned. */
            if (self->dishes[self->loc] == -1)
                break;

            /* Attempt to get the dish. */
            if (ask_dish(ctx, self, &msg, response)) {
                *price += (int)response->dish.price;
                self->wait_time += response->dish.eating_time;
                collected++;
                self->dishes[self->loc] = (ssize_t)response->dish.id;
            } else {
                /* Dish unavailable or station empty. */
                self->dishes[self->loc] = -1;
            }
            break;

        case CHECKOUT:
            /* ======================== GROUP WAITING ==========================
             * Clients must wait for all members of their group before paying.
             * ==================================================================
             */
            sem_wait(ctx->sem[shm]);
            group->members_ready++;

            if (group->members_ready == group->total_members) {
                /* Last member arrived: wake up everyone else. */
                it(i, 0, group->total_members - 1) { sem_signal(group->sem); }
                group->members_ready = 0;
                sem_signal(ctx->sem[shm]);

            } else {
                /* Wait for the rest of the group. */
                sem_signal(ctx->sem[shm]);
                sem_wait(group->sem);
            }

            /* If no food was collected (all stations empty), exit early. */
            if (collected == 0) {
                zprintf(
                    ctx->sem[out],
                    "CLIENT %d: Fasting (nothing left or gave up), exiting.\n",
                    self->pid
                );
                return;
            }

            /* ====================== PAYMENT PROCESS ======================== */
            send_msg(self->msgq, msg, sizeof(msg_t) - sizeof(long));
            recive_msg(self->msgq, self->pid, response);
            break;

        case TABLE:
            zprintf(
                ctx->sem[out], "CLIENT: %d, %d, WAITING TABLE\n", self->pid,
                self->loc
            );

            /* Acquire a seat at a table. */
            sem_wait(ctx->sem[tbl]);

            zprintf(
                ctx->sem[out], "CLIENT %d: Found table, Eating for %zu ns\n",
                self->pid, self->wait_time
            );

            /* Simulate eating time accumulated from dishes. */
            znsleep(self->wait_time);

            zprintf(ctx->sem[out], "CLIENT %d: Leaving table\n", self->pid);

            /* Release the seat. */
            sem_signal(ctx->sem[tbl]);
            break;

        case EXIT:
            zprintf(ctx->sem[out], "CLIENT: %d, EXIT\n", self->pid);
            return;
        }

        /* Move to the next logical location/state. */
        self->loc++;
    }
}

/**
 * @brief Communicates with a station worker to request a specific dish.
 * * If the preferred dish is finished, the client attempts to find an
 * alternative dish from the same station before giving up.
 *
 * @param ctx      Simulation context.
 * @param self     Pointer to current client.
 * @param msg      The outgoing request message.
 * @param response Pointer to store the received worker response.
 * @return true    If a dish was successfully obtained.
 * @return false   If the station is empty or all alternatives are finished.
 */
static bool
ask_dish(const simctx_t *ctx, client_t *self, msg_t *msg, msg_t *response) {
    bool tried[MAX_ELEMENTS];
    memset(tried, 0, sizeof(tried));

    /* Mark the initial preferred dish as tried. */
    if (msg->dish.id < MAX_ELEMENTS)
        tried[msg->dish.id] = true;

    while (ctx->is_sim_running) {
        send_msg(self->msgq, *msg, sizeof(msg_t) - sizeof(long));
        zprintf(
            ctx->sem[out], "CLIENT %d: Asking for dish %zu at Station %d\n",
            self->pid, msg->dish.id, self->loc
        );

        int res = recive_msg(self->msgq, self->pid, response);
        if (res == -1) {
            return false;
        }

        /* Success: dish obtained. */
        if (response->status == RESPONSE_OK) {
            zprintf(
                ctx->sem[out], "CLIENT %d: Obtained dish %zu\n", self->pid,
                response->dish.id
            );
            return true;
        }

        /* Case 1: The entire station category (e.g., all First Courses) is out
         * of stock. */
        if (response->status == RESPONSE_CATEGORY_FINISHED) {
            zprintf(
                ctx->sem[out],
                "CLIENT %d: Station %d empty, skipping course.\n", self->pid,
                self->loc
            );
            return false;
        }

        /* Case 2: Specific dish is out of stock, but others might be available.
         */
        if (response->status == RESPONSE_DISH_FINISHED) {
            zprintf(
                ctx->sem[out],
                "CLIENT %d: Dish %zu finished, looking for alternative...\n",
                self->pid, msg->dish.id
            );

            ssize_t      new_id    = -1;
            const size_t menu_size = ctx->menu[self->loc].size;

            /* Strategy: Try 10 random picks first for variety. */
            for (int k = 0; k < 10; k++) {
                size_t r       = (size_t)rand() % menu_size;
                size_t real_id = ctx->menu[self->loc].data[r].id;
                if (real_id < MAX_ELEMENTS && !tried[real_id]) {
                    new_id = (ssize_t)real_id;
                    break;
                }
            }

            /* If random picks failed, perform a linear search for any remaining
             * dish. */
            if (new_id == -1) {
                for (size_t i = 0; i < menu_size; i++) {
                    size_t real_id = ctx->menu[self->loc].data[i].id;
                    if (real_id < MAX_ELEMENTS && !tried[real_id]) {
                        new_id = (ssize_t)real_id;
                        break;
                    }
                }
            }

            /* No alternatives left. */
            if (new_id == -1) {
                zprintf(
                    ctx->sem[out],
                    "CLIENT %d: Tried all dishes in category %d, giving up.\n",
                    self->pid, self->loc
                );
                return false;
            }

            /* Update request and retry. */
            msg->dish.id  = (size_t)new_id;
            tried[new_id] = true;

        } else return false;
    }
    return false;
}
