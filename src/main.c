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

        if (s_getch() == 'q') {
            s_draw_text(screen, 2, 6, "QUITTING...");
            s_display(screen);
            break;
        } else         
            s_draw_text(screen, 2, 6, "Premi [q] per distruggere IPC e uscire.");

        s_display(screen);
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

void
sim_day(
    simctx_t *ctx,
    station  *stations,
    size_t    day,
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

    int users_inside = ctx->config.nof_users - sem_getval(ctx->sem.cl_end);
    
    // Se c'è overload o semplicemente fine tempo, quelli dentro non hanno finito -> non serviti
    if (users_inside > 0) {
        ctx->global_stats.users_not_served += users_inside;
    }

    // GESTIONE OVERLOAD
    // -----------------------------------
    if (users_inside >= ctx->config.overload_threshold) {
        zprintf(
            ctx->sem.out, "MAIN: Sim end overload (Users left: %d)\n",
            users_inside
        );
        ctx->is_sim_running = false;
    }
    
    it(i, 0, NOF_STATIONS) {
        ctx->global_stats.served_dishes += stations[i].stats.served_dishes;
        ctx->global_stats.earnings      += stations[i].stats.earnings;
        ctx->global_stats.total_breaks  += stations[i].stats.total_breaks;
        ctx->global_stats.worked_time   += stations[i].stats.worked_time;
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

    // Layout
    const int col_w    = s->cols / 2; 
    const int u_total  = ctx->config.nof_users;
    
    // Calcolo metriche
    int u_finished = sem_getval(ctx->sem.cl_end);
    int u_inside   = u_total - u_finished;
    int u_eating   = ctx->config.nof_tbl_seats - sem_getval(ctx->sem.tbl);

    // --- HEADER ---
    const char* title = "=== OASI DEL GOLFO - DASHBOARD ===";
    s_draw_text(s, (s->cols - strlen(title))/2, 1, title);
    
    s_draw_text(s, 2, 2, "GIORNO: %zu/%d | STATO: %s", 
                day, ctx->config.sim_duration, 
                ctx->is_day_running ? "APERTA" : "CHIUSA");

    // Bar Utenti (La barra usa i colori interni della tui.h, quindi ok)
    s_draw_text(s, 2, 4, "UTENTI NEL SISTEMA: %d/%d", u_inside, u_total);
    if (u_total > 0)
        s_draw_bar(s, col_w, 4, 30, (float)u_inside / (float)u_total);

    // --- COLONNA 1: FLUSSO & ECONOMIA ---
    int row = 6;
    s_draw_text(s, 2, row++, "[1. UTENTI]");
    
    size_t tot_served = 0;
    // Recuperiamo i non serviti totali dallo stato globale
    size_t tot_not_srv = ctx->global_stats.users_not_served; 
    
    it(i, 0, NOF_STATIONS) tot_served += st[i].stats.served_dishes;

    float avg_served = (day > 0) ? (float)tot_served/day : 0.0f;
    float avg_notsrv = (day > 0) ? (float)tot_not_srv/day : 0.0f;

    s_draw_text(s, 4, row++, "Serviti Totali:   %zu", tot_served);
    s_draw_text(s, 4, row++, "Serviti Media/Gg: %.1f", avg_served);
    s_draw_text(s, 4, row++, "Non Serviti Tot:  %zu", tot_not_srv);
    s_draw_text(s, 4, row++, "Non Serviti Avg:  %.1f", avg_notsrv);
    s_draw_text(s, 4, row++, "Seduti ai tavoli: %d", u_eating);

    row++;
    s_draw_text(s, 2, row++, "[2. ECONOMIA]");
    size_t revenue = st[CHECKOUT].stats.earnings;
    s_draw_text(s, 4, row++, "Incasso Tot:  %zu EUR", revenue);
    s_draw_text(s, 4, row++, "Media Giorn:  %.2f EUR", day > 0 ? (float)revenue/day : 0.0f);

    row++;
    s_draw_text(s, 2, row++, "[3. PAUSE OPERATORI]");
    size_t breaks = 0;
    it(i, 0, NOF_STATIONS) breaks += st[i].stats.total_breaks;
    
    s_draw_text(s, 4, row++, "Pause Totali: %zu", breaks);
    s_draw_text(s, 4, row++, "Media/Giorno: %.1f", day > 0 ? (float)breaks/day : 0.0f);
    
    // --- COLONNA 2: CIBO & PRESTAZIONI ---
    row = 6;
    int col_x = col_w + 2;

    s_draw_text(s, col_x, row++, "[4. STATISTICHE CIBO]");
    s_draw_text(s, col_x, row++, "TIPO       SERVITI    AVANZATI");
    
    const char* labels[] = {"Primi", "Secondi", "Caffe"};
    const int   types[]  = {FIRST_COURSE, MAIN_COURSE, COFFEE_BAR};

    it(i, 0, 3) {
        int t = types[i];
        size_t served = st[t].stats.served_dishes;
        
        char left_str[16];
        if (t == COFFEE_BAR) {
            sprintf(left_str, "Illimit."); // [cite: 109]
        } else {
            size_t leftovers = 0;
            it(k, 0, ctx->avl_dishes[t].size) 
                leftovers += ctx->avl_dishes[t].data[k].quantity;
            sprintf(left_str, "%zu", leftovers);
        }

        s_draw_text(s, col_x, row++, "%-10s %-10zu %-10s", labels[i], served, left_str);
    }

    row++;
    s_draw_text(s, col_x, row++, "[5. TEMPI MEDI (ns)]");
    
    // Media Globale Ponderata
    size_t sum_time = 0;
    size_t sum_ops  = 0;
    it(i, 0, NOF_STATIONS) {
        sum_time += st[i].stats.worked_time;
        sum_ops  += st[i].stats.served_dishes;
    }
    float global_wait = sum_ops > 0 ? (float)sum_time / sum_ops : 0.0f;

    s_draw_text(s, col_x, row++, "ATTESA MEDIA TOT: %.0f", global_wait);
    s_draw_text(s, col_x, row++, "------------------------");
    
    const char* st_names[] = {"Primi", "Main ", "Caffe", "Cassa"};
    it(i, 0, NOF_STATIONS) {
        float avg = st[i].stats.served_dishes > 0 ? 
            (float)st[i].stats.worked_time / st[i].stats.served_dishes : 0.0f;
        
        int active = ctx->config.nof_wk_seats[i] - sem_getval(st[i].wk_data.sem);
        
        // Mostra Tempo e Operatori Attivi/Totali
        s_draw_text(s, col_x, row++, "%s: %6.0f (Wk:%d/%zu)", 
                    st_names[i], avg, active, st[i].wk_data.cap);
    }

    // --- FOOTER ---
    s_draw_text(s, 2, s->rows - 2, "Premi [q] per terminare la simulazione.");
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

    it(loc, 0, 3) {
        struct available_dishes *dishes = &ctx->avl_dishes[loc];
        it(i, 0, ctx->menu[loc].size) {
            dishes->data[i].id = ctx->menu[loc].data[i].id;

            if (loc < COFFEE)
                dishes->data[i].quantity = ctx->config.avg_refill[loc];
            else
                dishes->data[i].quantity = 99999;
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

    it(i, 0, NOF_STATIONS - 1) {
        it(j, 0, NOF_STATIONS - i - 1) {
            size_t time_a = ctx->config.avg_srvc[g_priority_list[j]];
            size_t time_b = ctx->config.avg_srvc[g_priority_list[j + 1]];

            if (time_a < time_b) {
                int temp               = g_priority_list[j];
                g_priority_list[j]     = g_priority_list[j + 1];
                g_priority_list[j + 1] = temp;
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

    it(i, 0, NOF_STATIONS) msg_kill((int)ctx->id_msg_q[i]);

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
    it (i, 0, NOF_STATIONS)
        st[i].wk_data.cap = 0;

    int total_workers = ctx->config.nof_workers;

    it (i, 0, NOF_STATIONS) {
        st[i].wk_data.cap = 1;
    }

    int remaining = total_workers - NOF_STATIONS;
    if (remaining <= 0) return;

    size_t total_srvc_time = 0;
    it(i, 0, NOF_STATIONS) {
        total_srvc_time += ctx->config.avg_srvc[i];
    }

    if (total_srvc_time == 0) total_srvc_time = 1;
    int assigned_count = 0;
    
    it(type, 0, NOF_STATIONS) {
        size_t weight = ctx->config.avg_srvc[type];
        
        int extra_workers = (int)((weight * remaining) / total_srvc_time);
        
        st[type].wk_data.cap += extra_workers;
        assigned_count       += extra_workers;
    }

    int leftovers = remaining - assigned_count;
    int p_idx = 0;

    while (leftovers > 0) {
        loc_t target = g_priority_list[p_idx];
        
        st[target].wk_data.cap++;
        leftovers--;
        
        p_idx = (p_idx + 1) % NOF_STATIONS;
    }
}
