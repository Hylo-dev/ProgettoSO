/* This file contains the main() loop for the client process.
 * The process is lauched by main.c with init_client
 */

#include "msg.h"
#include "objects.h"
#include "tools.h"
#include <stddef.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <unistd.h>


// NOTE: The client will always pick a maximum of one
//       dish for the FIRST and one for the MAIN
void
pick_dishes(
    struct client_menu* menu,
    simctx_t *ctx
) {
    size_t rnd;

    rnd = (size_t)rand()%ctx->first_menu_size;
    menu->data[FIRST] = ctx->first_courses_menu[rnd].id;

    rnd = (size_t)rand()%ctx->main_menu_size;
    menu->data[MAIN]  = ctx->main_courses_menu[rnd].id;

    menu->cnt += 2;

    rnd = (size_t)rand()%(ctx->coffee_menu_size+1);
    if (rnd >= ctx->coffee_menu_size) return;
    
    menu->data[COFFEE] = ctx->coffee_menu[rnd].id;
    menu->cnt++;
}

int
main(
    int    argc,
    char** argv
) {
    // argv must be:
    // { exec name, bool ticket, shmid for the menu }
    if (argc != 3)
        panic("ERROR: Invalid client arguments for pid: %d", getpid());

    bool   ticket = (bool)  atoi(argv[1]);
    size_t shmid  = (size_t)atoi(argv[2]);

    simctx_t *ctx = (simctx_t*)zshmat(shmid, NULL, 0);
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
        if (self.loc < self.dishes.cnt) {
            send_msg(self.msgq, self.ticket ? 2:3, self.pid, self.dishes.data[self.loc]);
        }

        // chiamata bloccante, sta fermo qui finche' non riceve una risposta
        recive_msg(self.msgq, self.pid, &response);
        
        // TODO: controllo se il piatto richiesto e' lo stesso di quello restituito
        dish = response.dish;
        
        printf("RISPOSTA: \nid: %zu, name: %s", dish.id, dish.name);

        self.loc++;
    }
}
