/* This file contains the main() loop for the client process.
 * The process is lauched by main.c with init_client
 */

#include "msg.h"
#include "objects.h"
#include "tools.h"
#include <stddef.h>
#include <stdlib.h>
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

    struct client_menu menu;
    pick_dishes(&menu, ctx);
    
    client_t self = {
        .pid    = getpid(),
        .ticket = ticket,
        .loc    = FIRST_COURSE,
        .served = false,
        .msgq   = ctx->id_msg_q[FIRST_COURSE],
        .dishes = menu
    };

    msg_dish_t response;
    dish_t     dish;

    while (ctx->is_sim_running && self.loc != EXIT) {
        // zprintf(zpr_sem, "CLIEN_Q: %zu\n", self.msgq);

        if (self.loc < NOF_STATIONS) {
            self.msgq = ctx->id_msg_q[self.loc];
        }

        if (self.loc < self.dishes.cnt) {
            const msg_dish_t msg = {
                .mtype  = self.ticket ? 2:3,
                .client = self.pid,
                .dish   = {self.dishes.data[self.loc], "", 0, 0},
                .status = 1
            };

            send_msg(self.msgq, msg, sizeof(msg_dish_t)-sizeof(long));
        }

        // chiamata bloccante, sta fermo qui finche' non riceve una risposta
        recive_msg(self.msgq, self.pid, &response);
        
        // TODO: controllo se il piatto richiesto e' lo stesso di quello restituito
        dish = response.dish;
        
        zprintf(zpr_sem, "RISPOSTA: \ntype: %zu, price: %d\n\n", response.mtype, dish.price);

        self.loc++;
    }
}
