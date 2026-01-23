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
void init_client (shmid_t);
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
void      release_ctx(shmid_t, simctx_t*);
station*  init_stations(simctx_t*, size_t);
void      release_station(station);

void      release_clients(simctx_t*);

void      kill_all_child(simctx_t*, station*);

screen*   init_scr();
void      kill_scr(screen* s);

void
render_dashboard(
    screen*,
    simctx_t*,
    station*,
    size_t
);

int
main(void) {
    /* ========================== INIT ========================== */
    signal(SIGUSR1, SIG_IGN);
    srand((unsigned int)time(NULL));
    screen* screen = init_scr();

    const size_t    ctx_shm  = zshmget(sizeof(simctx_t));
          simctx_t* ctx      = init_ctx(ctx_shm);

    const size_t    st_shm   = zshmget(sizeof(station) * NOF_STATIONS);
          station*  stations = init_stations(ctx, st_shm);

    g_client_pids.id  = zcalloc(ctx->config.nof_users, sizeof(pid_t));
    g_client_pids.cnt = 0;


    it (i, 0, ctx->config.nof_users)
        init_client(ctx_shm);

    assign_roles(ctx, stations);
    it(type, 0, NOF_STATIONS) {
        const size_t cap = stations[type].wk_data.cap;

        // the `k` index will be the index where the worker will put its data
        it(k, 0, cap) init_worker(ctx_shm, st_shm, k, (loc_t)type);
    }

    it(i, 0, ctx->config.sim_duration) {
        if (!ctx->is_sim_running) break; 
        sim_day(ctx, stations, i, screen);
    }

    ctx->is_sim_running = false; 
    ctx->is_day_running = false;

    zprintf(ctx->sem.out, "MAIN: Fine sim\n");

    while (true) {
        s_clear(screen);
        s_draw_text(screen, 2, 2, "--- SIMULAZIONE COMPLETATA ---");
        s_draw_text(screen, 2, 4, "Statistiche finali pronte.");
        s_draw_text(screen, 2, 6, "Premi [q] per distruggere IPC e uscire.");
        s_display(screen);

        if (s_getch() == 'q') break;
        usleep(100000);
    }

    it(i, 0, NOF_STATIONS)
        release_station(stations[i]);

    shmdt(stations);
    shm_kill(st_shm);
    
    release_clients(ctx);

    sem_set(ctx->sem.wall, ctx->config.nof_workers + ctx->config.nof_users);
    while(wait(NULL) > 0);

    release_ctx(ctx_shm, ctx);
    
    kill_scr(screen);
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
    sem_set(ctx->sem.wall,   ctx->config.nof_workers + ctx->config.nof_users);
    sem_set(ctx->sem.wk_end, ctx->config.nof_workers);
    sem_set(ctx->sem.cl_end, ctx->config.nof_users  );

    size_t min = 0;
    const size_t avg_refill_time = ctx->config.avg_refill_time;
    while (ctx->is_sim_running && min < WORK_DAY_MINUTES) {
        for (int i = 0; i < 5; i++) {
            render_dashboard(s, ctx, stations, day + 1);
    
            if (s_getch() == 'q') {
                ctx->is_sim_running = false;
                return;
            }
            usleep(50000); 
        }
        
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
    const int size_sem_cl = sem_getval(ctx->sem.cl_end);
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

    kill_all_child(ctx, stations);
    
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
            itos((int)role  ),
            NULL 
        };

        execve("./bin/worker", args, NULL);
        panic("ERROR: Execve failed launching a worker\n");
    }
}

void
init_client(
    const shmid_t ctx_id
) {
    const pid_t pid = zfork();

    if (pid == 0) {
        char *args[] = {
            "client",
            rand()%2 ? "1":"0",
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
    reset_terminal();
    free_screen(s);
}

void
render_dashboard(
    screen   *s,
    simctx_t *ctx,
    station  *st,
    size_t    day
) {
    s_clear(s);

    size_t title_pad = 2;
    size_t datas_pad = 4;
    size_t center_x = s->cols/2;

    size_t bar_width = 20;
    
    // --- INTESTAZIONE ---
    int    inside = ctx->config.nof_users - sem_getval(ctx->sem.cl_end); 
    s_draw_text(s, title_pad, 1,
                  "=== DASHBOARD RESPONSABILE MENSA - GIORNO %zu ===", day);
    s_draw_text(s, title_pad, 2,
                  "Stato: %s", 
                  ctx->is_day_running ? "APERTA" : "CHIUSA");

    s_draw_text(s, center_x, 2, "| Utenti in Mensa: ");

    s_draw_bar (s, center_x+5, 3, bar_width, inside/(float)ctx->config.nof_users);
    
    s_draw_text(s, center_x+5+bar_width, 3, " %d/%d",
                                            inside,
                                            ctx->config.nof_users);

    // --- 1. STATISTICHE UTENTI ---
    size_t total_served = 0;
    for(int i=0; i<4; i++) total_served += st[i].stats.served_dishes;

    size_t tables = ctx->config.nof_tbl_seats;

    s_draw_text(s, title_pad, 5, "[1. UTENTI]");
    s_draw_text(s, datas_pad, 6, "Serviti (Tot Simulation): %5zu",
                                  total_served);
    s_draw_text(s, datas_pad, 7, "Media Giornaliera:       %5.2f",
                                  (float)total_served / day);
    s_draw_text(s, datas_pad, 8, "In attesa ai tavoli:      %5d",
                                  tables - sem_getval(ctx->sem.tbl));

    // --- 2. TEMPI DI ATTESA MEDI (Per Stazione) ---
    s_draw_text(s, center_x + title_pad, 5, "[2. ATTESA MEDIA PER STAZIONE]");
    const char* st_names[] = {"PRIMI:", "SECONDI:", "COFFEE:", "CASSA:"};
    it (i, 0, NOF_STATIONS) {
        float avg_wait = st[i].stats.served_dishes > 0 ? 
            (float)st[i].stats.worked_time / st[i].stats.served_dishes : 0;
        s_draw_text(s, center_x + datas_pad, 6 + i,
                    "%-8s %5.2f ns", st_names[i], avg_wait);
    }
    
    // --- 3. OPERATORI E PAUSE ---
    size_t total_breaks = 0;
    for(int i=0; i<4; i++) total_breaks += st[i].stats.total_breaks;
    
    s_draw_text(s, title_pad, 11, "[3. LAVORO E PAUSE]");
    s_draw_text(s, datas_pad, 12, "Pause Totali Sim: %zu",
                                              total_breaks);
    s_draw_text(s, datas_pad, 13, "Media Pause/Giorno: %.2f",
                                             (float)total_breaks / day);
    
    // Monitoraggio lavoratori attivi istantaneo
    for (int i = 0; i < NOF_STATIONS; i++) {
        int active = ctx->config.nof_wk_seats[i] - sem_getval(st[i].wk_data.sem);
        s_draw_text(s, datas_pad, 14 + i, "ST %d Operatori: %d/%zu",
                                                      i+1, active, st[i].wk_data.cap);
    }

    // --- 4. STATISTICHE CIBO ---
    s_draw_text(s, center_x + title_pad, 11, "[4. CIBO E DISTRIBUZIONE]");
    
    // Intestazione con spazi fissi per facilitare l'allineamento
    s_draw_text(s, center_x + datas_pad, 12, "TIPOLOGIA      DISTRIBUITI   AVANZATI");
    
    for (int loc = 0; loc < 2; loc++) {
        size_t leftover = 0;
        for(size_t j = 0; j < ctx->avl_dishes[loc].size; j++) {
            leftover += ctx->avl_dishes[loc].data[j].quantity;
        }
        s_draw_text(s, center_x + datas_pad, 13 + loc, "%-14s %11zu %10zu", 
                      loc == 0 ? "PRIMI:" : "SECONDI:", 
                      st[loc].stats.served_dishes, 
                      leftover);
    }

    // --- 5. ECONOMIA ---
    s_draw_text(s, title_pad, 19, "[5. BILANCIO ECONOMICO]");
    size_t total_revenue = st[CHECKOUT].stats.earnings;
    s_draw_text(s, datas_pad, 20, "Incasso Totale:    %6zu EUR", total_revenue);
    s_draw_text(s, datas_pad, 21, "Media Giornaliera: %6zu EUR",
                                    (size_t)total_revenue / day);

    // --- 6. DEBUG ---
    size_t sem_wall = sem_getval(ctx->sem.wall);
    size_t sem_wk = sem_getval(ctx->sem.wk_end);
    size_t sem_cl = sem_getval(ctx->sem.cl_end);
    s_draw_text(s, center_x+title_pad, 19, "[6. DEBUG]");
    s_draw_text(s, center_x+datas_pad, 21, "Sem WALL:  %d", sem_wall);
    s_draw_text(s, center_x+datas_pad, 22, "Sem WK:    %d", sem_wk);
    s_draw_text(s, center_x+datas_pad, 20, "Sem CL:    %d", sem_cl);

    s_draw_text(s, title_pad, 25, "Premi [q] per terminare la simulazione");
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

void
release_ctx(
    shmid_t   shmid,
    simctx_t* ctx
) {
    sem_kill(ctx->sem.shm);
    sem_kill(ctx->sem.out);
    sem_kill(ctx->sem.tbl);
    sem_kill(ctx->sem.wall);
    sem_kill(ctx->sem.wk_end);
    sem_kill(ctx->sem.cl_end);
    
    it(i, 0, NOF_STATIONS)
        msg_kill((int)ctx->id_msg_q[i]);

    shmdt(ctx);

    shm_kill(shmid); 
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
release_station(station st) {
    const worker_t *wks = get_workers(st.wk_data.shmid);
    
    it(j, 0, st.wk_data.cap) {
        kill(wks[j].pid, SIGUSR1);
        msg_kill(wks[j].queue);
    }
    
    shmdt(wks);

    shm_kill(st.wk_data.shmid);
    sem_kill(st.wk_data.sem);
    sem_kill(st.sem);
}


void
release_clients(simctx_t* ctx) {
    it(i, 0, ctx->config.nof_users)
        kill(g_client_pids.id[i], SIGUSR1);
    // NOTE: here we'll put the group sem_kill, NOT IMPLEMENTED YET
}

void
kill_all_child(
    simctx_t *ctx,
    station  *stations
) {
    it(i, 0, NOF_STATIONS) {
        const worker_t *wks = get_workers(stations[i].wk_data.shmid);
        it(j, 0, stations[i].wk_data.cap) {
            kill(wks[j].pid, SIGUSR1);
        }
    }
    
    it(i, 0, ctx->config.nof_users) {
        kill(g_client_pids.id[i], SIGUSR1);
    }
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
