/* This file contains the main() loop for the client process.
 * The process is lauched by main.c with init_client
 */

#include "const.h"
#include "msg.h"
#include "objects.h"
#include "tools.h"
#include <stddef.h>
#include <stdlib.h>
#include <sys/_types/_ssize_t.h>
#include <unistd.h>

// NOTE: The client will always pick a maximum of one
//       dish for the FIRST and one for the MAIN
void
pick_dishes(
           ssize_t*,
    const  simctx_t*
);


static dish_t
ask_dish(
    const simctx_t*,
    client_t,
    msg_t,
    int,
    msg_t* 
);


void
send_request(
    const simctx_t*,
    const sem_t,
    const sem_t,
          client_t,
          msg_t*,
          int*
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
     * } */
    if (argc != 3)
        panic("ERROR: Invalid client arguments for pid: %d", getpid());

    const bool   ticket  = atob(argv[1]);
    const size_t shmid   = atos(argv[2]);

    while (true) {
        const simctx_t *ctx = get_ctx(shmid);
        zprintf(
            ctx->sem.out,
            "CLIENT: Waiting new day\n"
        );
        sem_wait(ctx->sem.wall);

        const sem_t out_sem = ctx->sem.out;
        const sem_t tbl_sem = ctx->sem.tbl;

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
        send_request(ctx, out_sem, tbl_sem, self, &response, &price);

        if (!ctx->is_sim_running) {
            zprintf(ctx->sem.out, "CLIENT %d: Giornata finita, esco.\n", getpid());
            break;
        }
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

        rnd = (ssize_t)(rand() % (ctx->menu[FIRST].size + 1) - 1); // -1 ..< size
        if (rnd != -1) {
            cur_loc = FIRST_COURSE;
            menu[FIRST] = (ssize_t)ctx->menu[FIRST].data[rnd].id;
            
        } else {
            cnt_nf++;
            menu[FIRST] = -1;
        }

        rnd = (ssize_t)(rand() % (ctx->menu[MAIN].size + 1) - 1); // -1 ..< size
        if (rnd != -1) {
            if (cur_loc == -1) cur_loc = MAIN;
            menu[MAIN] = (ssize_t)ctx->menu[MAIN].data[rnd].id;
            
        } else {
            cnt_nf++;
            menu[MAIN] = -1;
        }
    
        rnd = (ssize_t)(rand() % (ctx->menu[COFFEE].size + 1) - 1);
        if (rnd != -1) {
            if (cur_loc == -1) cur_loc = COFFEE;
            menu[COFFEE] = (ssize_t)ctx->menu[COFFEE].data[rnd].id;
        } else {
            cnt_nf++;
            menu[COFFEE] = -1;            
        }
    } while(cnt_nf == 3);
}
void
send_request(
    const simctx_t *ctx,
    const sem_t     out_sem,
    const sem_t     tbl_sem,
          client_t  self,
          msg_t    *response,
          int      *price
){
    while (ctx->is_sim_running && ctx->is_day_running) {
        if (self.loc < NOF_STATIONS)
            self.msgq = ctx->id_msg_q[self.loc];

        const msg_t msg = {
            /* l'mtype contiene:
                 * la priorita' della richiesta se e' il cliente a mandarlo;
                 * il pid del cliente se e' una riposta da parte del worker */
            .mtype  = (self.loc == CHECKOUT && self.ticket)?
                          TICKET : DEFAULT,
            .client = self.pid,
            .dish   = {
                self.loc < 3 ? (size_t)self.dishes[self.loc] : 0,
                "", 0, 0
            },
            .status = REQUEST_OK,
            .price  = self.loc == CHECKOUT ? (size_t)price : 0
        };

        switch (self.loc) {
            case FIRST_COURSE:
            case MAIN_COURSE:
            case COFFEE_BAR:
                if (self.dishes[self.loc] == -1)
                    break;

                const dish_t dish = ask_dish(
                    ctx,
                    self,
                    msg,
                    out_sem,
                    response
                );

                price += (int)dish.price;
                self.dishes[self.loc] = (int)dish.id;

                break;

            case CHECKOUT:

                send_msg(self.msgq, msg, sizeof(msg_t)-sizeof(long));
                recive_msg(self.msgq, self.pid, response);

                break;

            case TABLE:
                zprintf(
                    out_sem,
                    "CLIENT: %d, %d, WAITING TABLE\n",
                    self.pid, self.loc
                );

                sem_wait(tbl_sem);

                zprintf(
                    out_sem,
                    "CLIENT %d: Found table, Eating for %zu ns\n",
                    self.pid,
                    self.wait_time
                );
                znsleep(self.wait_time);

                zprintf(
                    out_sem,
                    "CLIENT %d: Leaving table\n",
                    self.pid
                );
                sem_signal(tbl_sem);
                break;

            case EXIT:
                zprintf(
                    out_sem,
                    "CLIENT: %d, EXIT\n",
                    self.pid
                );
                exit(1);
        }

        self.loc++;
    }
}


static dish_t
ask_dish(
    const simctx_t *ctx,
          client_t  self,
          msg_t     msg,
    const int       zpr_sem,
          msg_t    *response
) {
    do {
        send_msg(
            self.msgq,
            msg,
            sizeof(msg_t)-sizeof(long)
        );

        zprintf(
            zpr_sem,
            "CLIENT: id %d, loc %d, WAITING\n",
            self.pid, self.loc
        );

        recive_msg(self.msgq, self.pid, response);

        zprintf(
            zpr_sem,
            "CLIENT: %d, SERVED\n",
            self.pid
        );

        if (response->status == ERROR) {
            const size_t menu_size = ctx->menu[self.loc].size;

            if (menu_size > 1) {
                size_t temp;
                while ((temp = (size_t)rand()%menu_size) == msg.dish.id);
                msg.dish.id  = temp;
                msg.mtype    = HIGH;

            } else
                return msg.dish;
        }

        self.wait_time += response->dish.eating_time;

    } while (ctx->is_sim_running && response->status == ERROR);

    return msg.dish;
}
