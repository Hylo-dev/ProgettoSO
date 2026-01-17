#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "const.h"
#include "msg.h"
#include "objects.h"
#include "tools.h"

static inline station*
get_station(
    shmid_t shmid,
    loc_t   role
) {
    return &(get_stations(shmid))[role];
}

void
serve_client(
    const worker_t*,
          simctx_t*,
          station*,
          msg_t*,
    const double
);

int
main(
    int    argc,
    char** argv
) {
    // argv must be:
    /* {
        execname,
        shm_id,
        stations_shmid
        role,
    } */
    if (argc != 4)
        panic("ERROR: Invalid worker arguments for pid: %d", getpid());

    /* ====================== INIT ======================= */
    
    const shmid_t   ctx_id  = atos(argv[1]);
    const shmid_t   sts_id  = atos(argv[2]);
    const size_t    role    = atos(argv[3]);

          simctx_t* ctx     = get_ctx(ctx_id);
          station*  st      = get_station(sts_id, role);
          worker_t* wks     = get_workers(st->wk_data.shmid);

    const sem_t     shm_sem = ctx->sem.shm;
    const sem_t     out_sem = ctx->sem.out; 

    const size_t    queue   = ctx->id_msg_q[role];

          double    variance = var_srvc[role];
          msg_t     response;
          size_t    services;
    
    // attatch self to the mem area in the shm
    sem_wait(st->sem);
    size_t idx = st->wk_data.cnt++;
    wks[idx] = (worker_t) {
        .pid        = getpid(),
        .role       = role,
        .pause_time = 0,
        .queue      = queue,
    };
    worker_t *self  = &wks[idx];
    sem_signal(st->sem);

    /* ===================  WORK LOOP ==================== */
    while (ctx->is_sim_running) {

        /* ================== SEM OWNERSHIP ================== */
        struct timespec t_start, t_end;
        clock_gettime(CLOCK_REALTIME, &t_start); 
        sem_wait(st->wk_data.sem); 
        clock_gettime(CLOCK_REALTIME, &t_end);
        long wait_ns = (t_end.tv_sec - t_start.tv_sec) * 1000000000L + 
                       (t_end.tv_nsec - t_start.tv_nsec);
        self->pause_time += wait_ns;

        services = 0;
        
        /* ====================== WORK ====================== */
        while (ctx->is_sim_running) {
                    
            recive_msg(self->queue, -DEFAULT, &response);

            switch (self->role) {
                case COFFEE_BAR:
                case FIRST_COURSE:
                case MAIN_COURSE:
                case CHECKOUT:
                    serve_client( self, ctx, st, &response, variance );
                    break;
                case TABLE:
                case EXIT:
                    panic("ERROR: Invalid worker arguments for pid: %d\n",
                          getpid());
            }

            response.mtype = response.client;
            send_msg(
                self->queue,
                response,
                sizeof(msg_t) - sizeof(long)
            );

            services++;

            // CHECK_PAUSA: if the served clients are at least nof_pause
            // leaves the shift
            if (services >= ctx->config.nof_pause) {
                zprintf(ctx->sem.out, "Worker %d va in pausa dopo %d servizi\n", 
                        getpid(), services);
                break; 
            }
        }

        // 3. RILASCIO IL POSTO (Fine Turno)
        // ------------------------------------------------
        sem_signal(st->wk_data.sem);
        
        if (!ctx->is_sim_running) break;

        self->pause_time += ctx->config.stop_duration; 
        
        znsleep(ctx->config.stop_duration);
    }

    sem_signal(st->wk_data.sem);
    return 0;
}

static inline void
_serve_food(
          simctx_t   *ctx,
    const worker_t   *self,
          station    *st,
          msg_t *response,
          size_t      time
) {
    const size_t dish_id = response->dish.id;
    const sem_t  out_sem = ctx->sem.out;
    
    ssize_t actual_index = -1;
    struct available_dishes* dishes = &ctx->available_dishes[self->role];
    
    for (size_t i = 0; i < dishes->size; i++) {
        if (dishes->elements[i].id == dish_id) {
            actual_index = (ssize_t)i;
            break;
        }
    }

    if (actual_index != -1) {
        size_t *quantity_p = (size_t*)&dishes->elements[actual_index].quantity;
        const dish_t dish_info = ctx->menu[self->role].elements[actual_index];

        if (*quantity_p > 0) {
            // the BAR has infinite coffees, what a dream.
            *quantity_p -= (self->role != COFFEE_BAR) ? 1:0;

            // Conta quanti piatti sono stati serviti.
            st->stats.served_dishes++;
            st->stats.worked_time += time;
            
            response->status = RESPONSE_OK;
            response->dish   = dish_info;
        } else
            response->status = ERROR;

    } else {
        zprintf(
            ctx->sem.out,
            "ERRORE: ID Piatto %zu non trovato nel menu %d\n",
            dish_id, self->role
        );
        response->status = ERROR;
    }
}

static inline void
_serve_checkout(
    const worker_t   *self, // ronly
          station    *st,   // ronly
          msg_t *response
) {
    if (self->role == CHECKOUT)
        st->stats.earnings += response->price;
        
    response->status = RESPONSE_OK;
}

void
serve_client(
    const worker_t   *self,
          simctx_t   *ctx,
          station    *st,
          msg_t *response,
    const double      variance
){

    size_t avg         = ctx->config.avg_srvc[self->role]; 
    size_t actual_time = get_service_time(avg, variance);

    zprintf(
        ctx->sem.out,
        "WORKER %d: Service time %zu ns\n",
        getpid(),
        actual_time
    );
    znsleep(actual_time);

    sem_wait(ctx->sem.shm);

    if (self->role != CHECKOUT)
        _serve_food(ctx, self, st, response, actual_time);    
    else
        _serve_checkout(self, st, response);

    sem_signal(ctx->sem.shm);
}
