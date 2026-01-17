#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "const.h"
#include "msg.h"
#include "objects.h"
#include "tools.h"

static inline station*
get_station(size_t stations_shmid, location_t role) {
    station* st = &(get_stations(stations_shmid))[role];
    return st;
}

void
serve_client(
    const worker_t*,
          simctx_t*,
          station*,
          msg_dish_t*,
    const int,
    const int,
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
        role_idx,
        zprt_sem,
        shm_sem,
        stations_shmid
    } */
    if (argc != 6)
        panic("ERROR: Invalid worker arguments for pid: %d", getpid());

    const size_t shm_id   = atos(argv[1]);
    const size_t role_idx = atos(argv[2]);
    const int    zpr_sem  = atoi(argv[3]);
    const int    shm_sem  = atoi(argv[4]);
    const size_t stations = atos(argv[5]);

          simctx_t   *ctx  = get_ctx(shm_id);
    const location_t  role = ctx->roles[role_idx].role;
          station    *st   = get_station(stations, role);
          worker_t   *wks  = get_workers(st->wk_data.shmid);

    const size_t queue = ctx->id_msg_q[role];

    sem_wait(st->sem);
    // NOTE: the firsts active workers will be the firsts to take the seat

    size_t idx = st->wk_data.cnt++;
    wks[idx] = (worker_t) {
        .pid        = getpid(),
        .active     = (st->wk_data.cnt <= ctx->config.nof_wk_seats[role]),
        .role       = role,
        .pause_time = 0,
        .queue      = queue,
    };

    worker_t *self  = &wks[idx];
    sem_signal(st->sem);
    
    double     variance = var_srvc[self->role];
    msg_dish_t response;
    while (ctx->is_sim_running) {
        recive_msg(self->queue, -DEFAULT, &response);

        switch (self->role) {
            case COFFEE_BAR:
            case FIRST_COURSE:
            case MAIN_COURSE:
            case CHECKOUT:
                serve_client(self, ctx, st, &response, shm_sem, zpr_sem, variance);
                break;

            case TABLE:
            case EXIT:
                panic("ERROR: Invalid worker arguments for pid: %d", getpid());
        }

        response.mtype = response.client;
        send_msg(self->queue, response, sizeof(msg_dish_t) - sizeof(long));
    }
}

static inline void
_serve_food(
          simctx_t   *ctx,
    const worker_t   *self,
          station    *st,
          msg_dish_t *response,
          size_t      time,
          int         zpr_sem
) {
    const size_t dish_id = response->dish.id;
    
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
        const dish_t current_dish_info = ctx->menu[self->role].elements[actual_index];

        if (*quantity_p > 0) {
            // the BAR has infinite coffees, what a dream.
            *quantity_p -= self->role != COFFEE_BAR ? 1:0;

            // Conta quanti piatti sono stati serviti.
            st->stats.served_dishes++;
            st->stats.worked_time += time;

            response->status = RESPONSE_OK;
            response->dish   = current_dish_info;
            
        } else { response->status = ERROR; }
            
    } else {
        zprintf(
            zpr_sem,
            "ERRORE: ID Piatto %zu non trovato nel menu %d\n",
            dish_id, self->role
        );
        response->status = ERROR;
    }
}

static inline void
_serve_checkout(
    const worker_t   *self,
          station    *st,
          msg_dish_t *response
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
          msg_dish_t *response,
    const int         shm_sem,
    const int         zpr_sem,
    const double      variance
){

    size_t avg         = ctx->config.avg_srvc[self->role]; 
    size_t actual_time = get_service_time(avg, variance);

    zprintf(zpr_sem, "WORKER %d: Service time %zu ns\n", getpid(), actual_time);
    znsleep(actual_time);

    sem_wait(shm_sem);

    if (self->role == CHECKOUT) {
        _serve_checkout(self, st, response);
                
    } else {
        _serve_food(ctx, self, st, response, actual_time, zpr_sem);
    }

    sem_signal(shm_sem);
}
