#include <signal.h>
#include <stdint.h>
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


static int g_priority_list[4];
static struct {
    pid_t* id;
    size_t cnt;
} g_client_pids;

volatile sig_atomic_t g_users_req = 0; // Flag per il segnale
volatile sig_atomic_t g_stop_req  = 0; // Gesrione CTRL+C
static shmid_t g_shmid = 0;            // Per passare l'ID a init_client

void
handler_new_users(int sig) {
    (void)sig;
    g_users_req = 1;
}

void
handler_stop(int sig) {
    (void)sig;
    g_stop_req = 1;
}

/* Prototipi */
void
init_groups(
    simctx_t*,
    shmid_t
);
void init_client (shmid_t, size_t);
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
    screen*,
    bool*
);
void assign_roles(
    const simctx_t*,
          station*
);
simctx_t* init_ctx(size_t, conf_t);
void      release_ctx(shmid_t, simctx_t*);
station* init_stations(simctx_t*, size_t);
void      release_station(station);

void      write_shared_data(shmid_t, shmid_t);
void      reset_shared_data();

int       process_new_users(simctx_t *ctx);

void      release_clients(simctx_t*);

// SENDS SIGTERM (simulation ended)
void      kill_all_child(simctx_t*, station*, int sig);

screen* init_scr();
void      kill_scr(screen* s);

void render_final_report(screen *s, simctx_t *ctx, station *st, bool stopped);

void
render_dashboard(
    screen*,
    simctx_t*,
    station*,
    size_t,
    size_t,
    int
);

int
main(int argc, char **argv) {

    conf_t conf = {};
    switch (argc) {
    case 1:
        load_config("data/default_config.json", &conf);
        break;
    case 2:
        load_config(argv[1], &conf);
        break;
    default:
        fprintf(stderr, "ERROR: Too many arguments provided.\n");
        fprintf(stderr, "Usage: %s [config_file_path]\n", argv[0]);

        fprintf(stderr, "Received command: ");
        it(i, 0, argc) { fprintf(stderr, "%s ", argv[i]); }
        fprintf(stderr, "\n");
        return -1;
    }

    fclear("data/simulation.log");
    fclear("data/stats.csv");

    /* ========================== INIT ========================== */
    signal(SIGUSR1, SIG_IGN);
    srand((unsigned int)time(NULL));
    screen* screen = init_scr();

    
    struct sigaction sa_stop;
    memset(&sa_stop, 0, sizeof(sa_stop));
    sa_stop.sa_handler = handler_stop;
    sigaction(SIGINT, &sa_stop, NULL);  // Ctrl+C
    sigaction(SIGTERM, &sa_stop, NULL); // kill standard
    // -----------------------------------------------------------

    const size_t ctx_shm  = zshmget(
        sizeof(simctx_t) + (conf.nof_users * sizeof(struct groups_t))
    );

    simctx_t* ctx = init_ctx(ctx_shm, conf);

    const size_t    st_shm   = zshmget(sizeof(station) * NOF_STATIONS);
          station* stations = init_stations(ctx, st_shm);

    write_shared_data(ctx_shm, st_shm);

    g_client_pids.id  = zcalloc(ctx->config.nof_users, sizeof(pid_t));
    g_client_pids.cnt = 0;

    init_groups(ctx, ctx_shm);

    assign_roles(ctx, stations);
    it(type, 0, NOF_STATIONS) {
        const size_t cap = stations[type].wk_data.cap;

        // the `k` index will be the index where the worker will put its data
        it(k, 0, cap) init_worker(ctx_shm, st_shm, k, (loc_t)type);
    }

    bool manual_quit;
    it(i, 0, ctx->config.sim_duration) {
        if (!ctx->is_sim_running) break;
        sim_day(ctx, stations, i, screen, &manual_quit);
    }

    ctx->is_sim_running = false;
    ctx->is_day_running = false;

    zprintf(ctx->sem[out], "MAIN: Fine sim\n");
    render_final_report(screen, ctx, stations, manual_quit);

    reset_shared_data();

    it(i, 0, NOF_STATIONS)
        release_station(stations[i]);

    shmdt(stations);
    shm_kill(st_shm);

    release_clients(ctx);

    sem_set(ctx->sem[wall], ctx->config.nof_workers + ctx->config.nof_users);
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
    screen   *s,
    bool     *manual_quit
) {
    if (day > 0) assign_roles(ctx, stations);

    zprintf(ctx->sem[out], "MAIN: Inizio giornata\n");

    sem_wait(ctx->sem[shm]);
    ctx->is_day_running = true;
    sem_signal(ctx->sem[shm]);

    sem_set(ctx->sem[wall],   ctx->config.nof_workers + ctx->config.nof_users);
    sem_set(ctx->sem[wk_end], ctx->config.nof_workers);
    sem_set(ctx->sem[cl_end], ctx->config.nof_users  );

    size_t current_min = 0;
    size_t next_refill_min = get_service_time(ctx->config.avg_refill_time, var_srvc[4]);

    *manual_quit = false;

    while (ctx->is_sim_running && current_min < WORK_DAY_MINUTES) {
        int n_new = process_new_users(ctx);

        if (n_new > 0)
             sem_set(ctx->sem[cl_end], ctx->config.nof_users);

        if (current_min % DASHBOARD_UPDATE_RATE == 0 || n_new > 0)
            render_dashboard(s, ctx, stations, day + 1, current_min, n_new);

        char c = s_getch();
        if (c == 'q' || g_stop_req) {
            ctx->is_sim_running = false;
            *manual_quit        = true;
            g_stop_req          = 0;
            break;
        }

        znsleep(1);
        current_min++;

        if (current_min >= next_refill_min) {
            sem_wait(ctx->sem[shm]);
            it (loc_idx, 0, 2) {
                dish_avl_t  *elem_avl = ctx->avl_dishes[loc_idx].data;
                const size_t size_avl = ctx->avl_dishes[loc_idx].size;
                const size_t max      = ctx->config.max_porzioni[loc_idx];
                const size_t refill   = ctx->config.avg_refill[loc_idx];
                it (j, 0, size_avl) {
                    size_t* qty = &elem_avl[j].quantity;
                    *qty += refill;
                    if (*qty > max) *qty = max;
                }
            }
            sem_signal(ctx->sem[shm]);
            size_t interval = get_service_time(ctx->config.avg_refill_time, var_srvc[4]);
            next_refill_min = current_min + interval;
        }
    }

    zprintf(ctx->sem[out], "MAIN: Fine giornata\n");

    const int users_inside = sem_getval(ctx->sem[cl_end]);
    if (users_inside > 0)
        ctx->global_stats.users_not_served += users_inside;


    if (!(*manual_quit) && users_inside >= ctx->config.overload_threshold) {
        zprintf(ctx->sem[out], "MAIN: Sim end overload (Users left: %d)\n", users_inside);
        ctx->is_sim_running = false;
    }

    it(i, 0, NOF_STATIONS) {
        ctx->global_stats.served_dishes += stations[i].stats.served_dishes;
        ctx->global_stats.earnings      += stations[i].stats.earnings;
        ctx->global_stats.total_breaks  += stations[i].stats.total_breaks;
        ctx->global_stats.worked_time   += stations[i].stats.worked_time;

        stations[i].total_stats.served_dishes += stations[i].stats.served_dishes;
        stations[i].total_stats.earnings      += stations[i].stats.earnings;
        stations[i].total_stats.total_breaks  += stations[i].stats.total_breaks;
        stations[i].total_stats.worked_time   += stations[i].stats.worked_time;
    }

    save_stats_csv(ctx, stations, day);
    it(i, 0, NOF_STATIONS) {
        memset(&stations[i].stats, 0, sizeof(stats));
    }

    ctx->is_day_running = false;
    if (*manual_quit || !ctx->is_sim_running) {
        // QUIT TO FINAL REPORT
        kill_all_child(ctx, stations, SIGTERM);
    } else {
        // RESET AT DAY END
        kill_all_child(ctx, stations, SIGUSR1);

        zprintf(ctx->sem[out], "MAIN: Reset day sem\n");
        sem_wait_zero(ctx->sem[wk_end]);
        sem_wait_zero(ctx->sem[cl_end]);
    }
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

void
init_groups(
          simctx_t *ctx,
    const shmid_t   ctx_shm
) {
    size_t users_remaining = ctx->config.nof_users;
    size_t group_idx = 0;
    size_t members_current_group = 0;
    size_t target_size = 0;

    it (i, 0, ctx->config.nof_users) {

        if (members_current_group == 0) {
            target_size = (rand() % ctx->config.max_users_per_group) + 1;

            if (target_size > users_remaining)
                target_size = users_remaining;

            ctx->groups[group_idx].id            = group_idx;
            ctx->groups[group_idx].total_members = target_size;
            ctx->groups[group_idx].members_ready = 0;
            ctx->groups[group_idx].sem           = sem_init(0);

            members_current_group = target_size;
        }

        init_client(ctx_shm, group_idx);

        members_current_group--;
        users_remaining--;

        if (members_current_group == 0) group_idx++;
    }
}

void
init_client(
    const shmid_t ctx_id,
    const size_t  group_idx
) {
    const pid_t pid = zfork();
    char *ticket_attr = (rand() % 100 < 80) ? "1" : "0";

    if (pid == 0) {
        char *args[] = {
            "client",
            ticket_attr,
            itos((int)ctx_id),
            itos((int)group_idx),
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
render_final_report(screen *s, simctx_t *ctx, station *st, bool manual_quit) {
    const int users_finished = sem_getval(ctx->sem[cl_end]);
    const int limit_users    = ctx->config.overload_threshold;

    const bool is_disorder = ctx->is_disorder_active;
    const bool is_overload = !manual_quit && (users_finished >= limit_users);
    const bool is_timeout  = !manual_quit && !is_overload && !is_disorder;

    // --- CALCOLO DATI (Logica estratta da tools.h -> save_stats_csv) ---
    
    // 1. Avanzi (Snapshot finale della memoria)
    size_t left_primi = 0;
    it(k, 0, ctx->avl_dishes[FIRST_COURSE].size) {
        left_primi += ctx->avl_dishes[FIRST_COURSE].data[k].quantity;
    }

    size_t left_secondi = 0;
    it(k, 0, ctx->avl_dishes[MAIN_COURSE].size) {
        left_secondi += ctx->avl_dishes[MAIN_COURSE].data[k].quantity;
    }

    // 2. Totali per categoria (Dalle statistiche accumulate delle stazioni)
    const size_t tot_primi   = st[FIRST_COURSE].total_stats.served_dishes;
    const size_t tot_secondi = st[MAIN_COURSE].total_stats.served_dishes;
    const size_t tot_caffe   = st[COFFEE_BAR].total_stats.served_dishes;
    const size_t tot_dishes  = ctx->global_stats.served_dishes;

    // 3. Economia e Staff Globale
    const size_t tot_earn    = ctx->global_stats.earnings;
    const size_t tot_breaks  = ctx->global_stats.total_breaks;
    const size_t tot_unserved= ctx->global_stats.users_not_served;

    // --- SETUP TUI ---

    int         status_col;
    const char *title_status;
    char        reason[128];

    if (manual_quit) {
        status_col = COL_GRAY;
        title_status = "SIMULAZIONE INTERROTTA";
        snprintf(reason, sizeof(reason), "MOTIVO: Interruzione manuale (Q / CTRL+C)");
    } else if (is_disorder) {
        status_col = COL_RED;
        title_status = "TERMINAZIONE: COM. DISORDER";
        snprintf(reason, sizeof(reason), "MOTIVO: Blocco stazione cassa (Communication Disorder)");
    } else if (is_overload) {
        status_col = COL_RED;
        title_status = "TERMINAZIONE: OVERLOAD";
        snprintf(reason, sizeof(reason), "MOTIVO: Utenti in attesa (%d) > Soglia (%d)", users_finished, limit_users);
    } else if (is_timeout) {
        status_col = COL_GREEN;
        title_status = "TERMINAZIONE: TIMEOUT";
        snprintf(reason, sizeof(reason), "MOTIVO: Raggiunta durata massima (%d giorni)", ctx->config.sim_duration);
    }

    while (true) {
        s_clear(s);
        const int W = s->cols;
        const int H = s->rows;
        const int mid_x = W / 2;

        // --- HEADER ---
        draw_box(s, 0, 0, W, H, COL_WHITE);
        draw_box(s, 1, 1, W - 2, 5, status_col);

        size_t title_len = strlen(title_status);
        size_t reason_len = strlen(reason);
        s_draw_text(s, (W - title_len) / 2, 2, COL_WHITE, "%s", title_status);
        s_draw_text(s, (W - reason_len) / 2, 3, COL_WHITE, "%s", reason);

        int r = 8;
        int c1 = 4;
        int val_x = c1 + 24;

        s_draw_text(s, c1, r++, COL_WHITE, "\u25BA STATISTICHE GLOBALI");
        draw_hline(s, c1, r++, 35, COL_GRAY);

        s_draw_text(s, c1, r, COL_GRAY, "Incasso Totale:");
        s_draw_text(s, val_x, r++, COL_GREEN, "%zu €", tot_earn);

        s_draw_text(s, c1, r, COL_GRAY, "Pause Staff Totali:");
        s_draw_text(s, val_x, r++, COL_WHITE, "%zu", tot_breaks);

        r++; // Spaziatore

        s_draw_text(s, c1, r, COL_GRAY, "Utenti Non Serviti Tot:");
        uint8_t uns_col = (tot_unserved > 0) ? COL_RED : COL_WHITE;
        s_draw_text(s, val_x, r++, uns_col, "%zu", tot_unserved);

        s_draw_text(s, c1, r, COL_GRAY, "Piatti Serviti Tot:");
        s_draw_text(s, val_x, r++, COL_WHITE, "%zu", tot_dishes);

        float avg_ticket = (tot_dishes > 0) ? (float)tot_earn / tot_dishes : 0.0f;
        s_draw_text(s, c1, r, COL_GRAY, "Media EUR/Piatto:");
        s_draw_text(s, val_x, r++, COL_WHITE, "%.2f", avg_ticket);


        // --- COLONNA DESTRA: DETTAGLIO CUCINA ---
        int c2 = mid_x + 2;
        r = 8;

        s_draw_text(s, c2, r++, COL_WHITE, "\u25BA DETTAGLIO PORTATE & RIMANENZE");
        draw_hline(s, c2, r++, W - c2 - 4, COL_GRAY);

        // Intestazioni Tabella
        int off_cat = 0; 
        int off_srv = 14; 
        int off_rem = 26;

        s_draw_text(s, c2 + off_cat, r, COL_GRAY, "CATEGORIA");
        s_draw_text(s, c2 + off_srv, r, COL_GRAY, "SERVITI");
        s_draw_text(s, c2 + off_rem, r, COL_GRAY, "AVANZI");
        r++;
        draw_hline(s, c2, r++, W - c2 - 4, COL_GRAY);

        // Riga Primi
        s_draw_text(s, c2 + off_cat, r, COL_WHITE, "Primi");
        s_draw_text(s, c2 + off_srv, r, COL_WHITE, "%zu", tot_primi);
        s_draw_text(s, c2 + off_rem, r, COL_WHITE, "%zu", left_primi);
        r++;

        // Riga Secondi
        s_draw_text(s, c2 + off_cat, r, COL_WHITE, "Secondi");
        s_draw_text(s, c2 + off_srv, r, COL_WHITE, "%zu", tot_secondi);
        s_draw_text(s, c2 + off_rem, r, COL_WHITE, "%zu", left_secondi);
        r++;

        // Riga Caffè
        s_draw_text(s, c2 + off_cat, r, COL_WHITE, "Caffe");
        s_draw_text(s, c2 + off_srv, r, COL_WHITE, "%zu", tot_caffe);
        s_draw_text(s, c2 + off_rem, r, COL_GRAY, "\u221E"); 
        r++;

        // --- FOOTER ---
        int footer_y = H - 4;
        draw_hline(s, 1, footer_y, W - 2, COL_GRAY);

        s_draw_text(
            s, 4, footer_y + 1, COL_WHITE,
            "Premi [q] per distruggere le risorse IPC e uscire."
        );

        if (is_disorder || is_overload) {
            s_draw_text(
                s, 4, footer_y + 2, COL_RED,
                "ATTENZIONE: Parametri critici superati."
            );

        } else if (manual_quit) {
            s_draw_text(
                s, 4, footer_y + 2, COL_GRAY,
                "NOTA: Simulazione parziale. I dati potrebbero essere "
                "incompleti."
            );
        } else {
            s_draw_text(
                s, 4, footer_y + 2, COL_GREEN,
                "Simulazione conclusa con successo."
            );
        }

        s_display(s);

        char c = s_getch();
        if (c == 'q' || g_stop_req)
            break;

        usleep(100000);
    }
}

void
render_dashboard(
    screen   *s,
    simctx_t *ctx,
    station  *st,
    size_t    day,
    size_t    min,
    int       new_clients
) {
    s_clear(s);

    // Dimensioni
    const size_t W = s->cols;
    const size_t H = s->rows;
    const size_t mid_x = W / 2; // Punto centrale esatto

    static int notification_timer = 0;
    static int saved_user_count = 0;

    if (new_clients > 0) {
        notification_timer = TUI_NOTIFICATIONS_LEN;
        saved_user_count = new_clients;
    }

    // --- 1. CORNICE E TITOLO ---
    draw_box(s, 0, 0, W, H, COL_GRAY);

    // Titolo centrato (Riga 1, dentro il box)
    const char* title = " OASI DEL GOLFO - DASHBOARD ";
    size_t t_len = strlen(title);
    s_draw_text(s, (W - t_len)/2, 0, COL_WHITE, title);

    // Linea separatrice sotto l'header (Riga 2)
    draw_hline(s, 1, 2, W-2, COL_GRAY);
    s_draw_text(s, 0, 2, COL_GRAY, BOX_VR);   // ├
    s_draw_text(s, W-1, 2, COL_GRAY, BOX_VL); // ┤
    s_draw_text(s, mid_x, 2, COL_GRAY, BOX_HD); // ┬

    // --- 2. INFO HEADER ---
    s_draw_text(s, 2, 1, COL_GRAY, "GIORNO: ");
    s_draw_text(s, 10, 1, COL_WHITE, "%zu/%d", day, ctx->config.sim_duration);

    int u_total    = ctx->config.nof_users;
    int u_finished = sem_getval(ctx->sem[cl_end]);
    int u_inside   = u_total - u_finished;

    if (u_total > 0) {
        int bar_w = 20;
        float pct = (float)u_inside / (float)u_total;
        
        char count_buf[32];
        snprintf(count_buf, 32, "%3d/%3d", u_inside, u_total);
        int count_len = strlen(count_buf);

        int end_x   = W - 2;
        int count_x = end_x - count_len;   
        int bar_x   = count_x - 1 - bar_w; 
        int label_x = bar_x - 7;           

        if (label_x > (int)mid_x) { 
            s_draw_text(s, label_x, 1, COL_GRAY, "Users:");
            s_draw_bar(s, bar_x, 1, bar_w, pct, COL_WHITE, COL_GRAY);
            s_draw_text(s, count_x, 1, COL_WHITE, "%s", count_buf);
        }
    }

    draw_vline(s, mid_x, 3, H - 4, COL_GRAY);
    s_draw_text(s, mid_x, H-1, COL_GRAY, BOX_HU); // ┴

    // --- 3. COLONNA SINISTRA ---
    size_t r = 4;
    size_t c1 = 2;

    s_draw_text(s, c1, r++, COL_WHITE, "\u25BA STATISTICHE FLUSSO");
    r++;

    size_t tot_served = 0;
    size_t tot_breaks = 0;
    it(i, 0, NOF_STATIONS) {
        tot_served += st[i].stats.served_dishes;
        tot_breaks += st[i].stats.total_breaks;
    }
    size_t tot_not_srv = ctx->global_stats.users_not_served;
    size_t u_eating    = ctx->config.nof_tbl_seats - sem_getval(ctx->sem[tbl]);

    int label_w = 18;
    s_draw_text(s, c1 + 2, r, COL_GRAY, "Serviti Totali:");
    s_draw_text(s, c1 + 2 + label_w, r++, COL_WHITE, "%zu", tot_served);

    s_draw_text(s, c1 + 2, r, COL_GRAY, "Non Serviti:");
    s_draw_text(s, c1 + 2 + label_w, r++, COL_WHITE, "%zu", tot_not_srv);

    s_draw_text(s, c1 + 2, r, COL_GRAY, "Seduti ai tavoli:");
    s_draw_text(s, c1 + 2 + label_w, r++, COL_WHITE, "%d", u_eating);

    r += 2;

    s_draw_text(s, c1, r++, COL_WHITE, "\u25BA ECONOMIA & STAFF");
    r++;

    size_t revenue = st[CHECKOUT].stats.earnings;
    s_draw_text(s, c1 + 2, r, COL_GRAY, "Incasso:");
    s_draw_text(s, c1 + 2 + label_w, r++, COL_WHITE, "%zu€", revenue);

    s_draw_text(s, c1 + 2, r, COL_GRAY, "Pause Totali:");
    s_draw_text(s, c1 + 2 + label_w, r++, COL_WHITE, "%zu", tot_breaks);


    // --- 4. COLONNA DESTRA (CUCINA & TEMPI) ---
    r = 4;
    size_t c2 = mid_x + 3;

    s_draw_text(s, c2, r++, COL_WHITE, "\u25BA SITUAZIONE CUCINA");
    r++;

    int off_p = 0;  // Piatto
    int off_s = 14; // Serviti
    int off_r = 24; // Rimasti

    s_draw_text(s, c2 + off_p, r, COL_GRAY, "PIATTO");
    s_draw_text(s, c2 + off_s, r, COL_GRAY, "SERVITI");
    s_draw_text(s, c2 + off_r, r, COL_GRAY, "RIMASTI");
    r++;

    draw_hline(s, c2, r++, 32, COL_GRAY);

    const char* labels[] = {"Primi", "Secondi", "Caffe"};
    const int   types[]  = {FIRST_COURSE, MAIN_COURSE, COFFEE_BAR};

    it(i, 0, 3) {
        int t = types[i];
        
        s_draw_text(s, c2 + off_p, r, COL_WHITE, "%s", labels[i]);
        s_draw_text(s, c2 + off_s, r, COL_WHITE, "%zu", st[t].stats.served_dishes);

        if (t == COFFEE_BAR) {
             s_draw_text(s, c2 + off_r, r, COL_WHITE, "\u221E");
        } else {
            size_t leftovers = 0;
            it(k, 0, ctx->avl_dishes[t].size) leftovers += ctx->avl_dishes[t].data[k].quantity;
            uint8_t col = (leftovers < 10) ? COL_WHITE : COL_GRAY;
            s_draw_text(s, c2 + off_r, r, col, "%zu", leftovers);
        }
        r++;
    }

    r += 2;
    s_draw_text(s, c2, r++, COL_WHITE, "\u25BA EFFICIENZA STAZIONI");
    r++;

    const char* st_names[] = {"Primi", "Main ", "Caffe", "Cassa"};

    it(i, 0, NOF_STATIONS) {
        float avg = st[i].stats.served_dishes > 0 ?
            (float)st[i].stats.worked_time / st[i].stats.served_dishes : 0.0f;

        int cap = (int)st[i].wk_data.cap;
        int active = ctx->config.nof_wk_seats[i] - sem_getval(st[i].wk_data.sem);
        const worker_t *wks = get_workers(st[i].wk_data.shmid);

        s_draw_text(s, c2, r, COL_GRAY, "%s:", st_names[i]);
        s_draw_text(s, c2 + 7, r, COL_WHITE, "%4.0fns", avg);
        s_draw_text(s, c2 + 17, r, COL_WHITE, "%02d/%02d", active, cap);

        int bar_x = c2 + 22;
        s_draw_text(s, bar_x, r, COL_GRAY, "[");
        for(int k=0; k < cap; k++) {
            if (wks[k].paused)
                s_draw_text(s, bar_x + 1 + k, r, COL_GRAY, "_");
            else
                s_draw_text(s, bar_x + 1 + k, r, COL_WHITE, "\u25A0");
        }
        s_draw_text(s, bar_x + 1 + cap, r, COL_GRAY, "]");
        r++;
    }


    // --- 5. FOOTER ---
    int footer_y = s->rows - 4;

    if (notification_timer > 0) {
        s_draw_text(s, 2, footer_y, COL_GREEN, "!!! ARRIVATI %d NUOVI CLIENTI !!!", saved_user_count);
        notification_timer--;
    }

    s_draw_text(s, 2, H - 2, COL_GRAY, "Premi [q] per terminare la simulazione.");

    int box_w = 22;
    int box_x = mid_x + 2;
    int box_y = H - 2;

    s_draw_text(s, box_x, box_y, COL_GRAY, "SYS:[");

    if (ctx->is_disorder_active) {
        const char* glitch_chars[] = {"▂", "▃", "▄"};
        for (int i = 0; i < 15; i++) {
            if ((min + i) % 7 == 0 && i % 3 == 0) {
                s_draw_text(s, box_x + 5 + i, box_y, COL_RED, "%s", glitch_chars[i % 3]);
            } else {
                s_draw_text(s, box_x + 5 + i, box_y, COL_RED, "▁");
            }
        }
        int blink_state = (min / 2) % 4;
        if (blink_state == 0 || blink_state == 2) {
            s_draw_text(s, box_x + box_w + 2, box_y, COL_RED, "!ALERT!");
        } else if (blink_state == 1) {
            s_draw_text(s, box_x + box_w + 2, box_y, COL_GRAY, "!ALERT!");
        }
    } else {
        const char *pattern[] = {"▂", "▂", "▃", "▃", "▄", "▄", "▅", "▅", "▆", "▆", "▇", "▇", "▇", "▆", "▆", "▅", "▅", "▄", "▄", "▃", "▃", "▂", "▂"};
        const int pattern_count = sizeof(pattern) / sizeof(pattern[0]);
        const int view_w = 16;
        for (int i = 0; i < view_w; i++) {
            int idx = ((min * 2) + i) % pattern_count;
            s_draw_text(s, box_x + 5 + i, box_y, COL_GREEN, "%s", pattern[idx]);
        }
    }
    s_draw_text(s, box_x + 21, box_y, COL_GRAY, "]");

    // --- FIX FINALE: Ridisegna gli angoli del box per sicurezza ---
    // Questo corregge eventuali sovrascritture causate da header troppo lunghi
    s_draw_text(s, W - 1, 0, COL_GRAY, BOX_TR);
    s_draw_text(s, W - 1, H - 1, COL_GRAY, BOX_BR);

    s_display(s);
}

/* ========================== SUPPORT ========================== */


simctx_t*
init_ctx(
    const size_t shm_id,
    const conf_t conf
) {
    simctx_t* ctx = get_ctx(shm_id);

    memset(ctx, 0, sizeof(simctx_t));
    ctx->is_sim_running = true;

    ctx->config = conf;
    // load_config("data/config.json", &ctx->config);

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

    ctx->sem[out]      = sem_init(1);
    ctx->sem[shm]      = sem_init(1);
    ctx->sem[disorder] = sem_init(1);
    ctx->sem[wk_end]   = sem_init(0);
    ctx->sem[cl_end]   = sem_init(0);
    ctx->sem[wall]     = sem_init(0);
    ctx->sem[tbl]      = sem_init(ctx->config.nof_tbl_seats);

    // Inizializziamo la lista
    g_priority_list[0] = FIRST_COURSE;
    g_priority_list[1] = MAIN_COURSE;
    g_priority_list[2] = COFFEE_BAR;
    g_priority_list[3] = CHECKOUT;

    it(i, 0, NOF_STATIONS - 1) {
        it(j, 0, NOF_STATIONS - i - 1) {
            const size_t time_a = ctx->config.avg_srvc[g_priority_list[j]];
            const size_t time_b = ctx->config.avg_srvc[g_priority_list[j + 1]];

            if (time_a < time_b) {
                const int temp         = g_priority_list[j];
                g_priority_list[j]     = g_priority_list[j + 1];
                g_priority_list[j + 1] = temp;
            }
        }
    }

    ctx->is_disorder_active = false;
    ctx->added_users        = 0;

    return ctx;
}

void
release_ctx(
    shmid_t   shmid,
    simctx_t* ctx
) {
    it(i, 0, SEM_CNT) sem_kill(ctx->sem[i]);

    it(i, 0, ctx->config.nof_users) sem_kill(ctx->groups[i].sem);

    it(i, 0, NOF_STATIONS) msg_kill((int)ctx->id_msg_q[i]);

    shmdt(ctx);

    shm_kill(shmid);
}

void
write_shared_data(
    shmid_t ctx_shm,
    shmid_t st_shm
) {
    FILE *f = zfopen("data/shared", "w");
    fprintf(f, "%d, %d\n", (int)ctx_shm, (int)st_shm);
    fclose(f);

    FILE *f_pid = zfopen("data/main.pid", "w");
    fprintf(f_pid, "%d", getpid());
    fclose(f_pid);

    g_shmid = ctx_shm;

    struct sigaction sa_add;
    memset(&sa_add, 0, sizeof(sa_add));
    sa_add.sa_handler = handler_new_users;
    sigaction(SIGUSR2, &sa_add, NULL);
}

void
reset_shared_data() {
    FILE *f = zfopen("data/shared", "w");
    fprintf(f, "%d, %d\n", -1, -1);
    fclose(f);

    FILE *f_pid = zfopen("data/main.pid", "w");
    fprintf(f_pid, "%d", -1);
    fclose(f_pid);
}

int
process_new_users(simctx_t *ctx) {
    sem_wait(ctx->sem[shm]);
    int num_new = ctx->added_users;
    if (num_new > 0) {
        ctx->added_users = 0;
    }
    sem_signal(ctx->sem[shm]);

    if (num_new <= 0)
        return 0;

    zprintf(ctx->sem[out], "[MAIN] Ricevuta richiesta per %d nuovi utenti!\n", num_new);

    size_t old_count = g_client_pids.cnt;
    size_t new_total = old_count + num_new;
    pid_t *new_ptr   = zrealloc(g_client_pids.id, new_total * sizeof(pid_t));
    g_client_pids.id = new_ptr;

    it(i, 0, num_new) init_client(g_shmid, old_count + i);

    sem_wait(ctx->sem[shm]);
    ctx->config.nof_users += num_new;
    sem_signal(ctx->sem[shm]);

    return num_new;
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
        kill(wks[j].pid, SIGTERM);
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
        kill(g_client_pids.id[i], SIGTERM);
}

void
kill_all_child(
    simctx_t *ctx,
    station  *stations,
    int       sig
) {
    it(i, 0, NOF_STATIONS) {
        const worker_t *wks = get_workers(stations[i].wk_data.shmid);
        it(j, 0, stations[i].wk_data.cap) {
            kill(wks[j].pid, sig);
        }
    }

    it(i, 0, ctx->config.nof_users) {
        kill(g_client_pids.id[i], sig);
    }
}

void
assign_roles(
    const simctx_t *ctx,
          station  *st
) {
    it (i, 0, NOF_STATIONS)
        st[i].wk_data.cap = 0;

    const int total_workers = ctx->config.nof_workers;

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
        const loc_t target = g_priority_list[p_idx];

        st[target].wk_data.cap++;
        leftovers--;

        p_idx = (p_idx + 1) % NOF_STATIONS;
    }
}
