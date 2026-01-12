#include <stdio.h>
#include <unistd.h>

#include "msg.h"
#include "objects.h"
#include "tools.h"

void
serve_client(
    const worker_t    self,
    const simctx_t*   ctx,
          msg_dish_t* response,
    const int         shm_sem,
    const int         zpr_sem

) {
    const size_t dish_id = response->dish.id;

    sem_wait(shm_sem, 0);

    // self.curr_role == 4 ? 2 : self.curr_role
    ssize_t actual_index = -1;

    //zprintf(zpr_sem, "W_INFO: rtoi %d\n", rtoi);

    const size_t category_size = ctx->available_dishes[self.curr_role].size;

    for (size_t i = 0; i < category_size; i++) {
        if (ctx->available_dishes[self.curr_role].elements[i].id == dish_id) {
            actual_index = (ssize_t)i;
            break;
        }
    }

    if (actual_index == -1) {
        zprintf(zpr_sem, "ERRORE: ID Piatto %zu non trovato nel menu %d\n", dish_id, self.curr_role);
        response->status = ERROR;

    } else {

        // Usa actual_index invece di dish_id
        size_t *quantity_p = (size_t*)&ctx->available_dishes[self.curr_role].elements[actual_index].quantity;
        const dish_t current_dish_info = ctx->menu[self.curr_role].elements[actual_index];

        // zprintf(zpr_sem, "W_INFO: %d\n", current_dish_info.eating_time);

        if (*quantity_p > 0) {
            (*quantity_p)--;
            //zprintf(zpr_sem, "CE PRODOTTO (Rimasti: %zu)\n", *quantity_p);
            response->status = RESPONSE_OK;
            response->dish   = current_dish_info;

        } else {
            //zprintf(zpr_sem, "NON CE PRODOTTO\n");
            response->status = ERROR;
        }
    }

    sem_signal(shm_sem, 0);
}

int
main(
    int    argc,
    char** argv
) {
    // argv must be:
    // { exec name, size_t shm_id, size_t role_idx, int zprt_sem, int shm_sem }
    if (argc != 5)
        panic("ERROR: Invalid worker arguments for pid: %d", getpid());

    const size_t shm_id   = (size_t)atoi(argv[1]);
    const size_t role_idx = (size_t)atoi(argv[2]);
    const int    zpr_sem  =         atoi(argv[3]);
    const int    shm_sem  =         atoi(argv[4]);

    const simctx_t   *ctx = (simctx_t*)zshmat(shm_id, NULL, 0);
    const location_t role = ctx->roles[role_idx].role;

    const size_t queue = ctx->id_msg_q[role];
    worker_t self = {
        .pid        = getpid(),
        .active     = true,
        .curr_role  = role,
        .pause_time = 0,
        .queue      = queue,
    };

    // zprintf(zpr_sem, "WORKER_loc: %zu\n", self.curr_role);
    // zprintf(zpr_sem, "Sim: %d\n", ctx->is_sim_running);

    msg_dish_t response;
    while (ctx->is_sim_running) {
        // zprintf(
        //     zpr_sem,
        //     "WORKER: %d, %d, WAITING\n",
        //     self.pid, self.queue
        // );
        recive_msg(self.queue, -DEFAULT, &response);

        switch (self.curr_role) {
            case COFFEE_BAR:
                //zprintf(zpr_sem, "COFFEE_BAR worker thread started\n");
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
        send_msg(self.queue, response, sizeof(msg_dish_t) - sizeof(long));
    }
}
