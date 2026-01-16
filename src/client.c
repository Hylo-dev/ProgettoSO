/* This file contains the main() loop for the client process.
 * The process is lauched by main.c with init_client
 */

#include "const.h"
#include "msg.h"
#include "objects.h"
#include "tools.h"
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// NOTE: The client will always pick a maximum of one
//       dish for the FIRST and one for the MAIN
void
pick_dishes(
    struct client_menu *menu,
    const  simctx_t    *ctx,
           location_t  *loc
) {
    ssize_t rnd;
    ssize_t cur_loc;
    size_t  cnt_nf;
    
    do {
        cur_loc   = -1;
        cnt_nf    = 0;
        menu->cnt = 0;

        if (
           ctx->menu[FIRST ].size == 0 ||
           ctx->menu[MAIN  ].size == 0 ||
           ctx->menu[COFFEE].size == 0 

        ) { panic("ERROR: Menu not have dishes\n"); }

        rnd = (ssize_t)(rand() % (ctx->menu[FIRST].size + 1) - 1); // -1 ..< size
        if (rnd != -1) {
            cur_loc = FIRST_COURSE;
            menu->data[FIRST] = ctx->menu[FIRST].elements[rnd].id;
            
        } else {
            cnt_nf++;
            menu->data[FIRST] = -1;
        }

        rnd = (ssize_t)(rand() % (ctx->menu[MAIN].size + 1) - 1); // -1 ..< size
        if (rnd != -1) {
            if (cur_loc == -1) cur_loc = MAIN;
            menu->data[MAIN] = ctx->menu[MAIN].elements[rnd].id;
            
        } else {
            cnt_nf++;
            menu->data[MAIN] = -1;
        }
    
        rnd = (ssize_t)(rand() % (ctx->menu[COFFEE].size + 1) - 1);
        if (rnd != -1) {
            if (cur_loc == -1) cur_loc = COFFEE;
            menu->data[COFFEE] = ctx->menu[COFFEE].elements[rnd].id;
            
        } else {
            cnt_nf++;
            menu->data[COFFEE] = -1;            
        }
    
        menu->cnt = 3;

    } while(cnt_nf == 3);
    
    *loc = cur_loc;
}

int
main(
    int    argc,
    char** argv
) {
    // argv must be:
    // { exec name, bool ticket, shmid for the menu, zprint_sem, table_sem }
    if (argc != 5)
        panic("ERROR: Invalid client arguments for pid: %d", getpid());

    const bool      ticket    = atob(argv[1]);
    const size_t    shmid     = atos(argv[2]);
    const int       zpr_sem   = atoi(argv[3]);
    const int       table_sem = atoi(argv[4]);
    const simctx_t *ctx       = (simctx_t*)zshmat(shmid, NULL, 0);

    struct client_menu menu = {0};
    location_t tmp_lc;

    pick_dishes(&menu, ctx, &tmp_lc);

    client_t self  = {
        .pid       = getpid(),
        .ticket    = ticket,
        .loc       = tmp_lc,
        .served    = false,
        .msgq      = ctx->id_msg_q[FIRST_COURSE],
        .wait_time = 0,
        .dishes    = menu
    };

    msg_dish_t response;
    //zprintf(zpr_sem, "CLIENT_l: %d, %d\n", self.loc, EXIT);
    while (ctx->is_sim_running) {
        zprintf(
            zpr_sem,
            "CLIENT: %d, loc: %d\n",
            self.pid, self.loc
        );

        if (self.loc < NOF_STATIONS)
            self.msgq = ctx->id_msg_q[self.loc];

        msg_dish_t msg = {
            /* l'mtype contiene:
             * la priorita' della richiesta se e' il cliente a mandarlo;
             * il pid del cliente se e' una riposta da parte del worker */
            .mtype  = (self.loc == CHECKOUT && self.ticket)?
                        TICKET : DEFAULT,
            .client = self.pid,
            .dish   = {
                self.loc < 3 ? self.dishes.data[self.loc] : 0,
                "", 0, 0
            },
            .status = REQUEST_OK
        };

        switch (self.loc) {
            case FIRST_COURSE:
                if (self.loc == FIRST_COURSE && self.dishes.data[FIRST_COURSE] == -1)
                    break;
                
            case MAIN_COURSE:
                if (self.loc == MAIN_COURSE && self.dishes.data[MAIN_COURSE] == -1)
                    break;
                
            case COFFEE_BAR:
                if (self.loc == COFFEE_BAR && self.dishes.data[COFFEE_BAR] == -1)
                    break;
                
                do {
                    send_msg(
                        self.msgq,
                         msg,
                         sizeof(msg_dish_t)-sizeof(long)
                    );

                    zprintf(
                        zpr_sem,
                        "CLIENT: %d, %d, WAITING\n",
                        self.pid, self.msgq
                    );

                    // chiamata bloccante, sta fermo qui finche' non riceve una risposta
                    recive_msg(self.msgq, self.pid, &response);

                    zprintf(
                        zpr_sem,
                        "CLIENT: %d, SERVED\n",
                        self.pid
                    );

                    if (response.status == ERROR) {
                        const size_t menu_size = ctx->menu[self.loc].size;

                        if (menu_size > 1) {
                            size_t temp;
                            while ((temp = (size_t)rand()%menu_size) == msg.dish.id);
                            msg.dish.id  = temp;
                            msg.mtype    = HIGH;

                        } else { goto END; } // GOTO used because the REDIS creator uses it :)
                    }

                    // zprintf(zpr_sem, "C_INFO: %d\n", response.dish.eating_time);
                    self.wait_time += response.dish.eating_time;

                } while (ctx->is_sim_running && response.status == ERROR);

                END:
                self.dishes.data[self.loc] = msg.dish.id;
                break;

            case CHECKOUT:
                send_msg(self.msgq, msg, sizeof(msg_dish_t)-sizeof(long));

                zprintf(
                    zpr_sem,
                    "CLIENT: %d, %d, WAITING\n",
                    self.pid, self.loc
                );

                // chiamata bloccante, sta fermo qui finche' non riceve una risposta
                recive_msg(self.msgq, self.pid, &response);

                zprintf(
                    zpr_sem,
                    "CLIENT: %d, SERVED\n",
                    self.pid
                );
                break;

            case TABLE:
                zprintf(
                    zpr_sem,
                    "CLIENT: %d, %d, WAITING TABLE\n",
                    self.pid, self.loc
                );

                sem_wait(table_sem, 0);
                
                zprintf(
                    zpr_sem,
                    "CLIENT %d: Found table, Eating for %zu ns\n",
                    self.pid,
                    self.wait_time
                );
                znsleep(self.wait_time);

                zprintf(
                    zpr_sem,
                    "CLIENT %d: Leaving table\n",
                    self.pid
                );
                sem_signal(table_sem, 0);
                break;

            case EXIT:
                zprintf(
                    zpr_sem,
                    "CLIENT: %d, EXIT\n",
                    self.pid
                );
                exit(1);
        }

        self.loc++;
    }
}
