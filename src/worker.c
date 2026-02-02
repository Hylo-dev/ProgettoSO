#include <stddef.h>
#include <stdio.h>
#include <unistd.h>
#include <signal.h>

#include "const.h"
#include "msg.h"
#include "objects.h"
#include "tools.h"

static inline station*
get_station(
    const shmid_t shmid,
    const loc_t   role
) {
    return &(get_stations(shmid))[role];
}

void
serve_client(
    const worker_t*,
          simctx_t*,
          station*,
          msg_t*,
          double
);

void
work_shift(
    simctx_t*,
    station*,
    size_t,
    msg_t*,
    worker_t*
);

void
work_with_pause(
    simctx_t*,
    station*,
    size_t,
    msg_t*,
    worker_t*
);

int
main(
    int    argc,
    char** argv
) {
    /* ====================== CTRL+C HANDLE ==================== */
    signal(SIGINT, SIG_IGN);
    /* ========================================================= */
    
    // argv must be:
    /* {
        execname,
        shm_id,
        stations_shmid
        role,
    } */
    if (argc != 5)
        panic("ERROR: Invalid worker arguments for pid: %d", getpid());

    /* ====================== INIT ======================= */
    
    const shmid_t ctx_id  = atos(argv[1]);
    const shmid_t sts_id  = atos(argv[2]);
    const size_t  idx     = atos(argv[3]);
    const loc_t   role    = atos(argv[4]);

    struct sigaction sa;
    sa.sa_handler = handle_signal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags   = 0;
    sigaction(SIGUSR1, &sa, NULL);

    while (true) {
        simctx_t* ctx = get_ctx(ctx_id);
        zprintf(
            ctx->sem[out],
            "WORKER: WAITING INIT DAY\n"
        );
        sem_wait(ctx->sem[wall]);

        if (!ctx->is_sim_running) break;

        station  *st  = get_station(sts_id, role);
        worker_t *wks = get_workers(st->wk_data.shmid);

        const size_t    queue    = ctx->id_msg_q[role];
        const size_t    variance = var_srvc[role];
              msg_t     response;

        // attatch self to the mem area in the shm
        sem_wait(st->sem);
        wks[idx] = (worker_t) {
            .pid        = getpid(),
            .role       = role,
            .queue      = queue,
            .paused     = true,
            .nof_pause  = 0,
            .pause_time = 0,
        };
        worker_t *self  = &wks[idx];
        sem_signal(st->sem);

        /* ===================  WORK LOOP ==================== */
        work_with_pause(ctx, st, variance, &response, self);

        sem_wait(ctx->sem[wk_end]);
        if (!ctx->is_sim_running) {
            zprintf(ctx->sem[out], "WORKER %d: Giornata finita, esco.\n", getpid());
            break;
        }
    }
    
    return 0;
}

void
work_with_pause(
    simctx_t *ctx,
    station  *st,
    size_t    variance,
    msg_t    *response,
    worker_t *self
) {
    while (ctx->is_sim_running && ctx->is_day_running) {
        zprintf(
            ctx->sem[out],
            "WORKER: id %d, role %d, WAITING\n",
            self->pid, self->role
        );

        /* ================== SEM OWNERSHIP ================== */
        struct timespec t_start, t_end;
        clock_gettime(CLOCK_REALTIME, &t_start);
        int res;
        do {
            res = sem_wait(st->wk_data.sem);
        } while (res == -1 && errno == EINTR);
        clock_gettime(CLOCK_REALTIME, &t_end);
        const long wait_ns = (t_end.tv_sec - t_start.tv_sec) * TO_NANOSEC +
                             (t_end.tv_nsec - t_start.tv_nsec);
        self->pause_time += (size_t)wait_ns;

        /* ====================== WORK ====================== */
        self->paused = false;
        work_shift(ctx, st, variance, response, self);

        // 3. RILASCIO IL POSTO (Fine Turno)
        // ------------------------------------------------
        sem_signal(st->wk_data.sem);

        // AGGIORNAMENTO STATISTICHE
        // ------------------------------------------------
        sem_wait(ctx->sem[shm]); 
        st->stats.total_breaks++;
        sem_signal(ctx->sem[shm]);

        self->pause_time += (size_t)ctx->config.pause_duration;
        znsleep((size_t)ctx->config.pause_duration);
    }
}

void
work_shift(
    simctx_t *ctx,
    station  *st,
    size_t    variance,
    msg_t    *response,
    worker_t *self
) {
    while (ctx->is_sim_running && ctx->is_day_running) {
        const ssize_t res = recv_msg_np(self->queue, -DEFAULT, response);
        if (res == -1 && errno == EINTR)
            continue;

        switch (self->role) {
            case COFFEE_BAR:
            case FIRST_COURSE:
            case MAIN_COURSE:
            case CHECKOUT:
                serve_client(self, ctx, st, response, variance);
                break;

            case TABLE:
            case EXIT:
                panic("ERROR: Invalid worker arguments for pid: %d\n",
                      getpid());
        }

        response->mtype = response->client;
        send_msg(
            self->queue,
            *response,
            sizeof(msg_t) - sizeof(long)
        );

        if (self->nof_pause < (size_t)ctx->config.nof_pause) {
            if ((rand() % 100) < 15) { 
                int free_seats = sem_getval(st->wk_data.sem);
                int active_workers = (int)st->wk_data.cap - free_seats;

                sem_wait(st->sem);
                if (active_workers > 1) {
                    zprintf(
                        ctx->sem[out],
                        "WORKER %d: Vado in pausa (Pausa n.%zu/%d)\n",
                        getpid(), self->nof_pause + 1, ctx->config.nof_pause
                    );
                    self->paused = true;
                    self->nof_pause++;
                    sem_signal(st->sem);
                    break; 
                }
                sem_signal(st->sem);
            }
        }
    }
}

static inline void
_serve_food(
          simctx_t *ctx,
    const worker_t *self,
          station  *st,
          msg_t    *response,
          size_t    time
) {
    const size_t dish_id = response->dish.id;
    
    ssize_t actual_index = -1;
    struct available_dishes* dishes = &ctx->avl_dishes[self->role];
    
    for (size_t i = 0; i < dishes->size; i++) {
        if (dishes->data[i].id == dish_id) {
            actual_index = (ssize_t)i;
            break;
        }
    }

    if (actual_index != -1) {
        size_t *quantity_p = (size_t*)&dishes->data[actual_index].quantity;
        const dish_t dish_info = ctx->menu[self->role].data[actual_index];

        if (*quantity_p > 0) {
            // the BAR has infinite coffees, what a dream.
            *quantity_p -= (self->role != COFFEE_BAR) ? 1:0;

            // Conta quanti piatti sono stati serviti.
            st->stats.served_dishes++;
            st->stats.worked_time += time;
            
            response->status = RESPONSE_OK;
            response->dish   = dish_info;
        } else {
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
                    "WORKER: Piatto %zu finito, ma altri disponibili.\n",
                    dish_id
                );
                response->status = RESPONSE_DISH_FINISHED;
            } else {
                zprintf(
                    ctx->sem[out], "WORKER: Stazione %d completamente vuota!\n",
                    self->role
                );
                response->status = RESPONSE_CATEGORY_FINISHED;
            }
        }
    } else {
        zprintf(
            ctx->sem[out],
            "ERRORE: ID Piatto %zu non trovato nel menu %d\n",
            dish_id, self->role
        );
        response->status = ERROR;
    }
}

static inline void
_serve_checkout(
    simctx_t *ctx,
    station  *st,
    msg_t    *response
) {

    if (ctx->is_disorder_active) {
        zprintf(ctx->sem[out], "WORKER: Cassa guasta (Disorder)!\n");
        response->status = ERROR;
        return;
    }

    if (sem_wait(ctx->sem[disorder]) == -1) {
        response->status = ERROR;
        return;
    }

    sem_signal(ctx->sem[disorder]);

    // TODO FIX : during the disorder, at the start, the earings continue rising
          size_t price    = response->price;
    const size_t discount = (price * DISCOUNT_DISH) / 100;

    if (response->ticket) price -= discount;

    sem_wait(st->sem);
    st->stats.earnings += price;
    sem_signal(st->sem);

    response->status = RESPONSE_OK;
}

void
serve_client(
    const worker_t *self,
          simctx_t *ctx,
          station  *st,
          msg_t    *response,
    const double    variance
){

    const size_t avg         = ctx->config.avg_srvc[self->role];
    const size_t actual_time = get_service_time(avg, variance);

    zprintf(
        ctx->sem[out],
        "WORKER %d: Service time %zu ns\n",
        getpid(),
        actual_time
    );
    znsleep(actual_time);

    if (self->role != CHECKOUT) {
        sem_wait(ctx->sem[shm]);
        _serve_food(ctx, self, st, response, actual_time);
        sem_signal(ctx->sem[shm]);

    } else { _serve_checkout(ctx, st, response); }
}
