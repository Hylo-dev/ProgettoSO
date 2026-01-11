#include <stdio.h>
#include <unistd.h>

#include "msg.h"
#include "objects.h"
#include "tools.h"

int
main(
    int    argc,
    char** argv
) {
    // argv must be:
    // { exec name, size_t shm_id, loc_t role, int zprt_sem, int shm_sem }
    if (argc != 5)
        panic("ERROR: Invalid worker arguments for pid: %d", getpid());

    const size_t     shm_id   = (size_t)    atoi(argv[1]);
    const size_t     role_idx = (size_t)    atoi(argv[2]);
    const int        zpr_sem  =             atoi(argv[3]);
    const int        shm_sem  =             atoi(argv[4]);

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

    msg_dish_t response;
    while (ctx->is_sim_running) {
        // zprintf(zpr_sem, "WORKER_Q: %zu\n", self.queue);

        recive_msg(self.queue, -3, &response);

        const pid_t  client_pid = response.client;
        const size_t dish_id    = response.dish.id;

        sem_wait(shm_sem, 0);

        ssize_t actual_index = -1;
        const size_t category_size = ctx->available_dishes[self.curr_role].size;

        for (size_t i = 0; i < category_size; i++) {
            if (ctx->available_dishes[self.curr_role].elements[i].id == dish_id) {
                actual_index = i;
                break;
            }
        }

        if (actual_index == -1) {
            zprintf(zpr_sem, "ERRORE: ID Piatto %zu non trovato nel menu %d\n", dish_id, self.curr_role);
            response.status = -1;

        } else {

            // Usa actual_index invece di dish_id
            size_t *quantity_p = (size_t*)&ctx->available_dishes[self.curr_role].elements[actual_index].quantity;
            const dish_t current_dish_info = ctx->menu[self.curr_role].elements[actual_index];

            if (*quantity_p > 0) {
                (*quantity_p)--;
                //zprintf(zpr_sem, "CE PRODOTTO (Rimasti: %zu)\n", *quantity_p);
                response.status = 1;
                response.dish = current_dish_info;

            } else {
                //zprintf(zpr_sem, "NON CE PRODOTTO\n");
                response.status = -1;
            }
        }

        sem_signal(shm_sem, 0);

        response.mtype = client_pid;

        send_msg(self.queue, response, sizeof(msg_dish_t)-sizeof(long));
    }
}
