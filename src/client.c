/* This file contains the main() loop for the client process.
 * The process is lauched by main.c with init_client
 */

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
    struct client_menu* menu,
    const simctx_t *ctx
) {
    size_t rnd;

    rnd = (size_t)rand()%ctx->menu[FIRST].size;              // first_menu_size;
    menu->data[FIRST] = ctx->menu[FIRST].elements[rnd].id;   // ctx->first_menu[rnd].id;

    rnd = (size_t)rand()%ctx->menu[MAIN].size;               // ctx->main_menu_size;
    menu->data[MAIN]  = ctx->menu[MAIN].elements[rnd].id;    // ctx->main_menu[rnd].id;

    menu->cnt += 2;

    rnd = (size_t)rand()%(ctx->menu[COFFEE].size + 1);       // coffee_menu_size+1);
    if (rnd >= ctx->menu[COFFEE].size) return;

    menu->data[COFFEE] = ctx->menu[COFFEE].elements[rnd].id; // ctx->coffee_menu[rnd].id;
    menu->cnt++;
}

int
main(
    int    argc,
    char** argv
) {
    // argv must be:
    // { exec name, bool ticket, shmid for the menu, zprint_sem }
    if (argc != 4)
        panic("ERROR: Invalid client arguments for pid: %d", getpid());

    const bool   ticket  = (bool)     atoi(argv[1]);
    const size_t shmid   = (size_t)   atoi(argv[2]);
    const int    zpr_sem =            atoi(argv[3]);
    const simctx_t *ctx  = (simctx_t*)zshmat(shmid, NULL, 0);
   
    station *st = (station*)shmat(ctx->shmid_stations, NULL, 0);

    st->workers = (worker_t*)shmat(st->shmid_workers, NULL, 0);

    struct client_menu menu = {0};

    pick_dishes(&menu, ctx);

    client_t self  = {
        .pid       = getpid(),
        .ticket    = ticket,
        .loc       = FIRST_COURSE,
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
            .mtype  = self.loc == CHECKOUT && self.ticket ? TICKET : DEFAULT,
            .client = self.pid,
            .dish   = {
                self.loc < 3 ? self.dishes.data[self.loc] : 0,
                "", 0, 0
            },
            .status = REQUEST_OK
        };

        switch (self.loc) {
            case FIRST_COURSE:
            case MAIN_COURSE:
            case COFFEE_BAR:
                do {
                    send_msg(self.msgq, msg, sizeof(msg_dish_t)-sizeof(long));

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
                    "CLIENT: %d, %d, EATING\n",
                    self.pid, self.wait_time
                );
                znsleep(self.wait_time);
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
