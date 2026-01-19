#include <signal.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <stdlib.h>
#include <unistd.h>

#include <sys/shm.h>
#include <sys/msg.h>
#include <sys/wait.h>
#include <sys/sem.h>

#include "config.h"
#include "const.h"
#include "objects.h"
#include "tools.h"
#include "menu.h"
#include "tui.h"


// lista prioritaria per assegnare i lavoratori alle stazioni, calcolata una sola volta.
static int g_priority_list[4];
static struct {
    pid_t* id;
    size_t cnt;
} g_client_pids;


/* Prototipi */
void init_client (
    shmid_t,
    bool
);
void init_worker (
    const shmid_t,
    const shmid_t,
    const size_t,
    const loc_t
);
void sim_day (
    simctx_t*,
    station*,
    size_t,
    screen*
);
void assign_roles(
    const simctx_t*,
          station*
);
simctx_t* init_ctx(size_t);
station*  init_stations(simctx_t*, size_t);

screen* init_scr();
void    kill_scr(screen* s);

void
render_dashboard(
    screen   *s,
    simctx_t *ctx,
    station  *stations,
    size_t
);

int
main(void) {
    /* ========================== INIT ========================== */
    signal(SIGUSR1, SIG_IGN);
    srand((unsigned int)time(NULL));
    screen* s = init_scr();

    const size_t    ctx_shm = zshmget(sizeof(simctx_t));
          simctx_t* ctx     = init_ctx(ctx_shm);

    // // TODO: to remove in production
    // zprintf(ctx->sem.out, "shm:    %d\n", ctx->sem.shm   );
    // zprintf(ctx->sem.out, "out:    %d\n", ctx->sem.out   );
    // zprintf(ctx->sem.out, "tbl:    %d\n", ctx->sem.tbl   );
    // zprintf(ctx->sem.out, "wall:   %d\n", ctx->sem.wall  );
    // zprintf(ctx->sem.out, "wk_end: %d\n", ctx->sem.wk_end);
    // zprintf(ctx->sem.out, "cl_end: %d\n", ctx->sem.cl_end);
    // zprintf(ctx->sem.out, "\n");

    g_client_pids.id  = zcalloc(ctx->config.nof_users, sizeof(pid_t));
    g_client_pids.cnt = 0;
    
    const size_t   st_shm = zshmget(sizeof(station) * NOF_STATIONS);
          station* st     = init_stations(ctx, st_shm);

    const size_t client_with_ticket = (size_t)(ctx->config.nof_users * 0.8);
    it (i, 0, ctx->config.nof_users) {
        const bool has_ticket = (i < (int)client_with_ticket);
        init_client(ctx_shm, has_ticket);
    }

    assign_roles(ctx, st);
    it (type, 0, NOF_STATIONS) {
        const size_t cap = st[type].wk_data.cap;

        // the `k` index will be the index where the worker will put its data
        it (k, 0, cap)
            init_worker(ctx_shm, st_shm, k, (loc_t)type);
    }
    
    it(i, 0, ctx->config.sim_duration)
        sim_day(ctx, st, i, s);

    ctx->is_sim_running = false;
    zprintf(ctx->sem.out, "MAIN: Fine sim\n");

    set_sem(ctx->sem.wall, ctx->config.nof_workers + ctx->config.nof_users);
    while(wait(NULL) > 0);

    shmctl((int)ctx_shm, IPC_RMID, NULL);
    semctl(ctx->sem.shm   , 0, IPC_RMID);
    semctl(ctx->sem.out   , 0, IPC_RMID);
    semctl(ctx->sem.tbl   , 0, IPC_RMID);
    semctl(ctx->sem.wall  , 0, IPC_RMID);
    semctl(ctx->sem.wk_end, 0, IPC_RMID);
    semctl(ctx->sem.cl_end, 0, IPC_RMID);

    it(i, 0, NOF_STATIONS)
        msgctl((int)ctx->id_msg_q[i], IPC_RMID, NULL);

    kill_scr(s);
    return 0;
}

void sim_day(
          simctx_t *ctx,
          station  *stations,
    const size_t    day,
          screen   *s
) {
    if (day > 0) assign_roles(ctx, stations);

    zprintf(ctx->sem.out, "MAIN: Inizio giornata\n");
    ctx->is_day_running = true;

    // Settato per la quantita di wk attivi
    set_sem(ctx->sem.wall,   ctx->config.nof_workers + ctx->config.nof_users);
    set_sem(ctx->sem.wk_end, ctx->config.nof_workers);
    set_sem(ctx->sem.cl_end, ctx->config.nof_users  );

    size_t min = 0;
    const size_t avg_refill_time = ctx->config.avg_refill_time;
    while (ctx->is_sim_running && min < WORK_DAY_MINUTES) {
        render_dashboard(s, ctx, stations, day+1);
        
        const size_t refill_time = get_service_time(
            avg_refill_time,
            var_srvc[4]
        );

        znsleep(refill_time);

        min += avg_refill_time;
        
        sem_wait(ctx->sem.shm);
        it (loc_idx, 0, 2) {
                  dish_avl_t *elem_avl = ctx->avl_dishes[loc_idx].data;
            const size_t      size_avl = ctx->avl_dishes[loc_idx].size;
            const size_t      max      = ctx->config.max_porzioni[loc_idx];
            const size_t      refill   = ctx->config.avg_refill[loc_idx];
            
            it (j, 0, size_avl) {
                size_t* qty = &elem_avl[j].quantity;

                *qty += refill;
                if (*qty > max) *qty = max;
            }
        }
        sem_signal(ctx->sem.shm);
    }
    zprintf(ctx->sem.out, "MAIN: Fine giornata\n");
    const int size_sem_cl = get_sem_val(ctx->sem.cl_end);
    if (size_sem_cl >= ctx->config.overload_threshold) {
        zprintf(
            ctx->sem.out,
            "MAIN: Sim end overload\n"
        );
        ctx->is_sim_running = false;

        it(i, 0, NOF_STATIONS) {
            const worker_t *wks = get_workers(stations[i].wk_data.shmid);
            it(j, 0, stations[i].wk_data.cap) {
                kill(wks[j].pid, SIGUSR1);
            }
        }

        it(i, 0, ctx->config.nof_users) {
            kill(g_client_pids.id[i], SIGUSR1);
        }

        return;
    }

    ctx->is_day_running = false;

    it(i, 0, NOF_STATIONS) {
        const worker_t *wks = get_workers(stations[i].wk_data.shmid);
        it(j, 0, stations[i].wk_data.cap) {
            if (wks[j].pid > 0) {
                kill(wks[j].pid, SIGUSR1);
            }
        }
    }
    
    it(i, 0, ctx->config.nof_users) {
        kill(g_client_pids.id[i], SIGUSR1);
    }
    
    zprintf(ctx->sem.out, "MAIN: Reset day sem\n");
    sem_wait_zero(ctx->sem.wk_end);
    sem_wait_zero(ctx->sem.cl_end);

    zprintf(ctx->sem.out, "MAIN: Stats da implementare\n");
}

/* ========================== PROCESSES ========================== */ 

void
init_worker (
    const shmid_t ctx_id,
    const shmid_t st_id,
    const size_t  idx,
    const loc_t   role
) {
    const pid_t pid = zfork();

    if (pid == 0) {
        char *args[] = {
            "worker",
            itos((int)ctx_id),
            itos((int)st_id ),
            itos((int)idx   ),
            itos(     role  ),
            NULL 
        };

        execve("./bin/worker", args, NULL);
        panic("ERROR: Execve failed launching a worker\n");
    }
}

void init_client(
    const shmid_t ctx_id,
    const bool    has_ticket
) {
    const pid_t pid = zfork();

    if (pid == 0) {
        char *args[] = {
            "client",
            has_ticket ? "1":"0",
            itos((int)ctx_id),
            NULL
        };
        execve("./bin/client", args, NULL);
        panic("ERROR: Execve failed for client\n");
    }
    g_client_pids.id[g_client_pids.cnt++] = pid;
}

/* =========================== OUTPUT =========================== */ 

screen*
init_scr() {
    size_t rows, cols;
    get_terminal_size(&rows, &cols);
    screen *s = init_screen(rows, cols);
    enableRawMode();
    return s;
}

void
kill_scr(screen* s) {
    disableRawMode();
    free_screen(s);
}

void
render_dashboard(
    screen   *s,
    simctx_t *ctx,
    station  *stations,
    size_t    day
) {
    s_clear(s);
    
    /* ========================= TITLE ========================= */
    s_draw_text_h(s, 2, 1,
                  "--- MENSA SIMULATION DASHBOARD ---");
    s_draw_text_h(s, 2, 2,
                  "Day: %d | Status: %s",
                  day, ctx->is_day_running ? "RUNNING" : "STOPPED");

    // 2. Visualizzazione Tavoli (Semaforo)
    int free_seats = semctl(ctx->sem.tbl, 0, GETVAL);
    int occupied   = ctx->config.nof_tbl_seats - free_seats;

    s_draw_text_h(s, 2, 4,
                  "TABLES: [%d/%d]",
                  occupied, ctx->config.nof_tbl_seats);
    
    // Barra orizzontale semplice per i tavoli
    s_repeat_h(s, 15, 4, '#', occupied);

    // 3. Barre Verticali per Piatti Disponibili (Esempio Primi e Secondi)
    for (int loc = 0; loc < 2; loc++) {
        int start_x = 5 + (loc * 20);
        s_draw_text_h(s, start_x, 6, loc == 0 ? "PRIMI" : "SECONDI");
        
        for (size_t i = 0; i < ctx->avl_dishes[loc].size; i++) {
            size_t qty = ctx->avl_dishes[loc].data[i].quantity;
            size_t max = ctx->config.max_porzioni[loc];
            int bar_height = (qty * 10) / (max > 0 ? max : 1); // Scala a 10 caratteri
            
            s_repeat_v(s, start_x, 17, '|', bar_height);
            
            s_draw_text_h(s,
                start_x + (i * 2), 18,
                "%zu", ctx->avl_dishes[loc].data[i].id
            );
        }
    }

    // 4. Stazioni e Worker
    s_draw_text_h(s, 45, 6, "STATIONS STATUS");
    for (int i = 0; i < NOF_STATIONS; i++) {
        int active_wks = stations[i].wk_data.cap - semctl(stations[i].wk_data.sem, 0, GETVAL);
        s_draw_text_h(s, 45, 7 + i, "ST %d: %d/%zu Workers Active", i, active_wks, stations[i].wk_data.cap);
    }

    s_display(s);
}


/* ========================== SUPPORT ========================== */ 


simctx_t*
init_ctx(
    const size_t shm_id
) {
    simctx_t* ctx = get_ctx(shm_id);

    memset(ctx, 0, sizeof(simctx_t));
    ctx->is_sim_running = true;

    load_config("data/config.json", &ctx->config);
 
    it (i, 0, NOF_STATIONS)
        ctx->id_msg_q[i] = zmsgget(IPC_PRIVATE, IPC_CREAT | SHM_RW);

    load_menu("data/menu.json", ctx);

    it (loc, 0, 3){
        struct available_dishes *dishes = &ctx->avl_dishes[loc];
        it (i, 0, ctx->menu[loc].size) {
            dishes->data[i].id = ctx->menu[loc].data[i].id;

            if (loc < COFFEE) {
                dishes->data[i].quantity = ctx->config.avg_refill[loc];

            } else { dishes->data[i].quantity = 99999; }
            dishes->size++;
        }
    }

    ctx->sem.out    = sem_init(1);
    ctx->sem.shm    = sem_init(1);
    ctx->sem.wk_end = sem_init(0);
    ctx->sem.cl_end = sem_init(0);
    ctx->sem.wall   = sem_init(0);
    ctx->sem.tbl    = sem_init(ctx->config.nof_tbl_seats);

    // Inizializziamo la lista
    g_priority_list[0] = FIRST_COURSE;
    g_priority_list[1] = MAIN_COURSE;
    g_priority_list[2] = COFFEE_BAR;
    g_priority_list[3] = CHECKOUT;

    size_t avg1, avg2;
    it (i, 0, NOF_STATIONS) {
        it (j, 0, NOF_STATIONS) {
            avg1 = ctx->config.avg_srvc[g_priority_list[j]];
            avg2 = ctx->config.avg_srvc[g_priority_list[i]];
            if (avg1 > avg2) {
                int temp = g_priority_list[i];
                g_priority_list[i] = g_priority_list[j];
                g_priority_list[j] = temp;
            }
        }
    }

    return ctx;
}

station*
init_stations(
    simctx_t* ctx,
    size_t shmid
) {
    station *st = get_stations(shmid);
    memset(st, 0, sizeof(station) * NOF_STATIONS);

    it (i, 0, NOF_STATIONS) {
        st[i].type        = (loc_t)i;
        st[i].sem         = sem_init(1);
        st[i].wk_data.sem = sem_init(ctx->config.nof_wk_seats[i]);

        st[i].wk_data.shmid = zshmget(
            sizeof(worker_t) * ctx->config.nof_workers
        );

        if (i < CHECKOUT) {
            it (j, 0, ctx->menu[i].size)
                st[i].menu[j] = ctx->menu[i].data[j];
        }
    }

    return st;
}

void
assign_roles(
    const simctx_t *ctx,
          station  *st
) {
    // the num_workers will never be less than 4
    // (the load_conf fun gives an error in that case)
    it (i, 0, NOF_STATIONS)
        st[i].wk_data.cap = 0;

    const int num_workers = ctx->config.nof_workers;
    st[FIRST_COURSE].wk_data.cap++;
    st[MAIN_COURSE ].wk_data.cap++;
    st[COFFEE_BAR  ].wk_data.cap++;
    st[CHECKOUT    ].wk_data.cap++;
    
    it (i, 4, num_workers) {
        const loc_t target_station_id = g_priority_list[i % 4];
        
        st[target_station_id].wk_data.cap++;
    }
}
