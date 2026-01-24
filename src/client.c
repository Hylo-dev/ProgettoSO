/* This file contains the main() loop for the client process.
 * The process is lauched by main.c with init_client
 */

#include "const.h"
#include "msg.h"
#include "objects.h"
#include "tools.h"
#include <stddef.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>

// NOTE: The client will always pick a maximum of one
//       dish for the FIRST and one for the MAIN
void
pick_dishes(
           ssize_t*,
    const  simctx_t*
);


static bool
ask_dish(
    const simctx_t*,
          client_t*,
          msg_t*,
          msg_t* 
);


void
send_request(
    const  simctx_t*,
           client_t*,
           msg_t*,
           int*,
    struct groups_t*
);


int
main(
    const int    argc,
          char** argv
) {
    // argv must be:
    /* {
     *    exec name,
     *    ticket,
     *    ctx_shmid,
     *    group_id
     * } */
    if (argc != 4)
        panic("ERROR: Invalid client arguments for pid: %d", getpid());

    /* ====================== INIT ======================= */

    const bool   ticket  = atob(argv[1]);
    const size_t shmid   = atos(argv[2]);
    const size_t grp_id  = atos(argv[3]);

    struct sigaction sa;
    sa.sa_handler = handle_signal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags   = 0;
    sigaction(SIGUSR1, &sa, NULL);

    while (true) {
        const simctx_t  *ctx = get_ctx(shmid);
        struct groups_t *group = (struct groups_t*)&ctx->groups[grp_id];
        zprintf(
            ctx->sem.out,
            "CLIENT: Waiting new day\n"
        );
        sem_wait(ctx->sem.wall);

        if (!ctx->is_sim_running) break;

        client_t self  = {
            .pid       = getpid(),
            .ticket    = ticket,
            .loc       = FIRST_COURSE,
            .served    = false,
            .msgq      = ctx->id_msg_q[FIRST_COURSE],
            .wait_time = 0,
            .dishes    = {}
        };

        pick_dishes(self.dishes, ctx);

        msg_t response;
        int   price = 0;

        /* =============== BEGIN OF REQUEST LOOP ============== */
        send_request(ctx, &self, &response, &price, group);

        sem_wait(ctx->sem.cl_end);
        if (!ctx->is_sim_running) {
            zprintf(ctx->sem.out, "CLIENT %d: Giornata finita, esco.\n", getpid());
            break;
        }
    }

    return 0;
}

void
send_request(
    const  simctx_t *ctx,
           client_t *self,
           msg_t    *response,
           int      *price,
    struct groups_t *group

){
    size_t collected = 0;
    while (ctx->is_sim_running && ctx->is_day_running) {

        if (self->loc < NOF_STATIONS)
            self->msgq = ctx->id_msg_q[self->loc];

        msg_t msg = {
            /* l'mtype contiene:
                 * la priorita' della richiesta se e' il cliente a mandarlo;
                 * il pid del cliente se e' una riposta da parte del worker */
            .mtype  = (self->loc == CHECKOUT && self->ticket)?
                          TICKET : DEFAULT,
            .client = self->pid,
            .dish   = {
                self->loc < 3 ? (size_t)self->dishes[self->loc] : 0,
                "", 0, 0
            },
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
                if (self->dishes[self->loc] == -1)
                    break;

                if (ask_dish(ctx, self, &msg, response)) {
                    *price += (int)response->dish.price;
                    self->wait_time += response->dish.eating_time;
                    collected++;
                    
                    self->dishes[self->loc] = (ssize_t)response->dish.id;

                } else {
                    self->dishes[self->loc] = -1;
                }
                break;

            case CHECKOUT:
                if (collected == 0) {
                    zprintf(ctx->sem.out, "CLIENT %d: Digiuno (tutto finito o rinuncia), esco.\n", self->pid);

                    sem_wait(ctx->sem.shm);
                    group->total_members--;

                    if (group->members_ready >= group->total_members && group->total_members > 0) {
                        sem_set(group->sem, (int)group->total_members - 1);
                    }

                    sem_signal(ctx->sem.shm);
                    return;
                }

                send_msg(self->msgq, msg, sizeof(msg_t)-sizeof(long));
                recive_msg(self->msgq, self->pid, response);
                break;

            case TABLE:
                zprintf(
                    ctx->sem.out,
                    "CLIENT: %d, %d, WAITING TABLE\n",
                    self->pid, self->loc
                );

                sem_wait(ctx->sem.shm);
                group->members_ready++;

                if (group->members_ready == group->total_members) {
                    sem_signal(group->sem);
                    sem_signal(ctx->sem.shm);

                } else {
                    sem_signal(ctx->sem.shm);
                    sem_wait(group->sem);
                    sem_signal(group->sem);
                }

                // IMPORTANT: TODO: add clients groups support here
                sem_wait(ctx->sem.tbl);

                zprintf(
                    ctx->sem.out,
                    "CLIENT %d: Found table, Eating for %zu ns\n",
                    self->pid,
                    self->wait_time
                );
                znsleep(self->wait_time);

                zprintf(
                    ctx->sem.out,
                    "CLIENT %d: Leaving table\n",
                    self->pid
                );
                sem_signal(ctx->sem.tbl);
                break;

            case EXIT:
                zprintf(
                    ctx->sem.out,
                    "CLIENT: %d, EXIT\n",
                    self->pid
                );
                return;
        }

        self->loc++;
    }
}

void
pick_dishes(
           ssize_t  *menu,
    const  simctx_t *ctx
) {
    ssize_t rnd;
    ssize_t cur_loc;
    size_t  cnt_nf;
    
    do {
        cur_loc   = -1;
        cnt_nf    = 0;

        rnd = (ssize_t)(rand() % (ctx->menu[FIRST].size + 1) - 1); 
        if (rnd != -1) {
            cur_loc = FIRST_COURSE;
            menu[FIRST] = (ssize_t)ctx->menu[FIRST].data[rnd].id;

        } else {
            cnt_nf++;
            menu[FIRST] = -1;
        }

        rnd = (ssize_t)(rand() % (ctx->menu[MAIN].size + 1) - 1); 
        if (rnd != -1) {
            if (cur_loc == -1) cur_loc = MAIN;
            menu[MAIN] = (ssize_t)ctx->menu[MAIN].data[rnd].id;

        } else {
            cnt_nf++;
            menu[MAIN] = -1;
        }
    
        rnd = (ssize_t)(rand() % (ctx->menu[COFFEE].size + 1) - 1);
        if (rnd != -1) {
            menu[COFFEE] = (ssize_t)ctx->menu[COFFEE].data[rnd].id;
        } else {
            menu[COFFEE] = -1;            
        }

    } while(menu[FIRST] == -1 && menu[MAIN] == -1);
}

static bool
ask_dish(
    const simctx_t *ctx,
          client_t *self,
          msg_t    *msg,
          msg_t    *response
) {
    bool tried[MAX_ELEMENTS];
    memset(tried, 0, sizeof(tried));

    if (msg->dish.id < MAX_ELEMENTS) 
        tried[msg->dish.id] = true;

    while (ctx->is_sim_running) {
        
        send_msg(self->msgq, *msg, sizeof(msg_t) - sizeof(long));

        zprintf(ctx->sem.out, "CLIENT %d: Chiede piatto %zu a Stazione %d\n", 
                self->pid, msg->dish.id, self->loc);

        int res = recive_msg(self->msgq, self->pid, response);
        if (res == -1) {
            return false; 
        }

        if (response->status == RESPONSE_OK) {
            zprintf(ctx->sem.out, "CLIENT %d: Ottenuto piatto %zu\n", self->pid, response->dish.id);
            return true;
        }

        // CASO 2: Tutta la categoria è finita
        if (response->status == RESPONSE_CATEGORY_FINISHED) {
            zprintf(ctx->sem.out, "CLIENT %d: Stazione %d vuota, salto portata.\n", self->pid, self->loc);
            return false;
        }

        // CASO 3: Piatto specifico finito, ma ce ne sono altri [cite: 133]
        if (response->status == RESPONSE_DISH_FINISHED) {
            zprintf(ctx->sem.out, "CLIENT %d: Piatto %zu finito, cerco alternativa...\n", self->pid, msg->dish.id);
            
            ssize_t new_id = -1;
            const size_t menu_size = ctx->menu[self->loc].size;

            for(int k=0; k<10; k++) {
                size_t r = (size_t)rand() % menu_size;
                size_t real_id = ctx->menu[self->loc].data[r].id;
                if (real_id < MAX_ELEMENTS && !tried[real_id]) {
                    new_id = (ssize_t)real_id;
                    break;
                }
            }

            if (new_id == -1) {
                for (size_t i = 0; i < menu_size; i++) {
                    size_t real_id = ctx->menu[self->loc].data[i].id;
                    if (real_id < MAX_ELEMENTS && !tried[real_id]) {
                        new_id = (ssize_t)real_id;
                        break;
                    }
                }
            }

            if (new_id == -1) {
                zprintf(ctx->sem.out, "CLIENT %d: Provati tutti i piatti di %d, rinuncio.\n", self->pid, self->loc);
                return false;
            }

            msg->dish.id = (size_t)new_id;
            tried[new_id] = true;
            
        } else return false;
    }
    return false;
}
