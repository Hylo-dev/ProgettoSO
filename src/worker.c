#include <stddef.h>
#include <stdio.h>
#include <unistd.h>

#include "msg.h"
#include "objects.h"
#include "tools.h"

static inline station
get_station(size_t stations_shmid, location_t role) {
    station st = ((station*)shmat(stations_shmid, NULL, 0))[role];
    return st;
}

void
serve_client(
    const worker_t*,
    const simctx_t*,
          msg_dish_t*,
    const int,
    const int         
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

    const simctx_t*  ctx  = (simctx_t*)zshmat(shm_id, NULL, 0);
    const location_t role = ctx->roles[role_idx].role;
          station    st   = get_station(stations, role);
          worker_t*  wks  = zshmat(st.wk_data.shmid, NULL, 0);

    const size_t queue = ctx->id_msg_q[role];

    sem_wait(st.sem, 0);
    // NOTE: the firsts active workers will be the firsts to take the seat
    wks[st.wk_data.cnt++] = (worker_t) {
        .pid        = getpid(),
        .active     = (st.wk_data.cnt <= ctx->config.nof_wk_seats[role]),
        .role  = role,
        .pause_time = 0,
        .queue      = queue,
    };

    worker_t *self  = &wks[st.wk_data.cnt++];
    sem_signal(st.sem, 0);

    msg_dish_t response;
    while (ctx->is_sim_running) {
        recive_msg(self->queue, -DEFAULT, &response);

        switch (self->role) {
            case COFFEE_BAR:
            case FIRST_COURSE:
            case MAIN_COURSE:
                serve_client(self, ctx, &response, shm_sem, zpr_sem);
                break;

            case CHECKOUT:
                response.status = RESPONSE_OK;
                break;

            case TABLE:
            case EXIT:
                panic("ERROR: Invalid worker arguments for pid: %d", getpid());
        }

        response.mtype = response.client;
        send_msg(self->queue, response, sizeof(msg_dish_t) - sizeof(long));
    }
}


void
serve_client(
    const worker_t   *self,
    const simctx_t   *ctx,
          msg_dish_t *response,
    const int         shm_sem,
    const int         zpr_sem
){
    const size_t dish_id = response->dish.id;

    sem_wait(shm_sem, 0);

    ssize_t actual_index = -1;
    struct available_dishes dishes = ctx->available_dishes[self->role];

    for (size_t i = 0; i < dishes.size; i++) {
        if (dishes.elements[i].id == dish_id) {
            actual_index = (ssize_t)i;
            break;
        }
    }

    if (actual_index != -1) {
        size_t *quantity_p = (size_t*)&dishes.elements[actual_index].quantity;
        const dish_t current_dish_info = ctx->menu[self->role].elements[actual_index];

        if (*quantity_p > 0) {
            // the BAR has infinite coffees, what a dream.
            *quantity_p -= self->role != COFFEE_BAR ? 1:0;

            response->status = RESPONSE_OK;
            response->dish   = current_dish_info;
        } else
            response->status = ERROR;
            
    } else {
        zprintf(
            zpr_sem,
            "ERRORE: ID Piatto %zu non trovato nel menu %d\n",
            dish_id, self->role
        );
        response->status = ERROR;
    }

    sem_signal(shm_sem, 0);
}
