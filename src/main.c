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
    screen*
);
void assign_roles(
    const simctx_t*,
          station*
);
simctx_t* init_ctx(size_t, conf_t);
void      release_ctx(shmid_t, simctx_t*);
station*  init_stations(simctx_t*, size_t);
void      release_station(station);

void      write_shared_data(shmid_t, shmid_t);
void      reset_shared_data();

void      release_clients(simctx_t*);

void      kill_all_child(simctx_t*, station*);

screen*   init_scr();
void      kill_scr(screen* s);

void
render_dashboard(
    screen*,
    simctx_t*,
    station*,
    size_t,
    size_t
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

    /* ========================== INIT ========================== */
    signal(SIGUSR1, SIG_IGN);
    srand((unsigned int)time(NULL));
    screen* screen = init_scr();

    const size_t ctx_shm  = zshmget(
        sizeof(simctx_t) + (conf.nof_users * sizeof(struct groups_t))
    );
    simctx_t* ctx = init_ctx(ctx_shm, conf);

    const size_t    st_shm   = zshmget(sizeof(station) * NOF_STATIONS);
          station*  stations = init_stations(ctx, st_shm);

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

    it(i, 0, ctx->config.sim_duration) {
        if (!ctx->is_sim_running) break; 
        sim_day(ctx, stations, i, screen);
    }

    ctx->is_sim_running = false; 
    ctx->is_day_running = false;

    zprintf(ctx->sem[out], "MAIN: Fine sim\n");

    while (true) {
        s_clear(screen);
        s_draw_text(screen, 2, 2,COL_WHITE, "--- SIMULAZIONE COMPLETATA ---");
        s_draw_text(screen, 2, 4,COL_WHITE,  "Statistiche finali pronte.");

        if (s_getch() == 'q') {
            s_draw_text(screen, 2, 6,COL_WHITE, "QUITTING...");
            s_display(screen);
            break;
        } else         
                s_draw_text(screen, 2, 6,COL_WHITE, "Premi [q] per distruggere IPC e uscire.");

        s_display(screen);
        usleep(100000);
    }

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
    screen   *s
) {
    if (day > 0) assign_roles(ctx, stations);

    zprintf(ctx->sem[out], "MAIN: Inizio giornata\n");
    ctx->is_day_running = true;

    // Settato per la quantita di wk attivi
    sem_set(ctx->sem[wall],   ctx->config.nof_workers + ctx->config.nof_users);
    sem_set(ctx->sem[wk_end], ctx->config.nof_workers);
    sem_set(ctx->sem[cl_end], ctx->config.nof_users  );

    size_t current_min = 0;
    
    size_t next_refill_min = get_service_time(
        ctx->config.avg_refill_time, 
        var_srvc[4]
    );

    while (ctx->is_sim_running && current_min < WORK_DAY_MINUTES) {
        
        if (current_min % DASHBOARD_UPDATE_RATE == 0)
            render_dashboard(s, ctx, stations, day + 1, current_min);
        
        if (s_getch() == 'q') {
            ctx->is_sim_running = false;
            break;
        }

        znsleep(1); 
        current_min++;

        
        if (current_min >= next_refill_min) {
            
            zprintf(ctx->sem[out], "MAIN: Eseguo Refill al minuto %zu\n", current_min);

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

            size_t interval = get_service_time(
                ctx->config.avg_refill_time,
                var_srvc[4]
            );
            next_refill_min = current_min + interval;
        }
    }
    
    zprintf(ctx->sem[out], "MAIN: Fine giornata\n");

    int users_inside = ctx->config.nof_users - sem_getval(ctx->sem[cl_end]);
    
    if (users_inside > 0) {
        ctx->global_stats.users_not_served += users_inside;
    }

    if (users_inside >= ctx->config.overload_threshold) {
        zprintf(ctx->sem[out], "MAIN: Sim end overload (Users left: %d)\n", users_inside);
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
    
    zprintf(ctx->sem[out], "MAIN: Reset day sem\n");
    sem_wait_zero(ctx->sem[wk_end]);
    sem_wait_zero(ctx->sem[cl_end]);
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

    if (pid == 0) {
        char *args[] = {
            "client",
            rand()%2 ? "1":"0",
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
render_dashboard(
    screen   *s,
    simctx_t *ctx,
    station  *st,
    size_t    day,
    size_t    min
) {
    s_clear(s);

    // Dimensioni
    const size_t W = s->cols;
    const size_t H = s->rows;
    const size_t mid_x = W / 2; // Punto centrale esatto
    
    // --- 1. CORNICE E TITOLO ---
    draw_box(s, 0, 0, W, H, COL_GRAY);
    
    // Titolo centrato (Riga 1, dentro il box)
    const char* title = " OASI DEL GOLFO - DASHBOARD "; // Spazi per estetica
    size_t t_len = strlen(title); // Nota: solo ASCII qui, quindi strlen è ok per centrare
    s_draw_text(s, (W - t_len)/2, 0, COL_WHITE, title); // Disegna SUL bordo (y=0) ma con sfondo nero copre la linea

    // Linea separatrice sotto l'header (Riga 2)
    draw_hline(s, 1, 2, W-2, COL_GRAY);
    s_draw_text(s, 0, 2, COL_GRAY, BOX_VR);   // ├
    s_draw_text(s, W-1, 2, COL_GRAY, BOX_VL); // ┤
    s_draw_text(s, mid_x, 2, COL_GRAY, BOX_HD); // ┬ (Connettore a T in alto)

    // --- 2. INFO HEADER (Riga 1 - Dentro box sopra linea) ---
    // Sinistra: Data e Stato
    s_draw_text(s, 2, 1, COL_GRAY, "GIORNO: %s%zu/%d", ANSI_COLORS[COL_WHITE], day, ctx->config.sim_duration);

    // Destra: Barra Utenti
    int u_total    = ctx->config.nof_users;
    int u_finished = sem_getval(ctx->sem[cl_end]);
    int u_inside   = u_total - u_finished;
    
    if (u_total > 0) {
        int bar_w = 20; 
        int bar_x = W - bar_w - 15;
        float pct = (float)u_inside / (float)u_total;
        
        s_draw_text(s, bar_x - 7, 1, COL_GRAY, "Users:");
        s_draw_bar(s, bar_x, 1, bar_w, pct, COL_WHITE, COL_GRAY);
        s_draw_text(s, bar_x + bar_w + 1, 1, COL_WHITE, "%d/%d", u_inside, u_total);
    }

    draw_vline(s, mid_x, 3, H - 4, COL_GRAY);
    s_draw_text(s, mid_x, H-1, COL_GRAY, BOX_HU); // ┴ (T in basso)

    size_t r = 4;
    size_t c1 = 2; // Margine sinistro colonna 1
    
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
    
    // Sezione 2
    s_draw_text(s, c1, r++, COL_WHITE, "\u25BA ECONOMIA & STAFF");
    r++;
    
    size_t revenue = st[CHECKOUT].stats.earnings;
    s_draw_text(s, c1 + 2, r, COL_GRAY, "Incasso:");
    s_draw_text(s, c1 + 2 + label_w, r++, COL_WHITE, "%zu EUR", revenue);

    s_draw_text(s, c1 + 2, r, COL_GRAY, "Pause Totali:");
    s_draw_text(s, c1 + 2 + label_w, r++, COL_WHITE, "%zu", tot_breaks);


    // --- 5. COLONNA DESTRA (CUCINA & TEMPI) ---
    r = 4;
    size_t c2 = mid_x + 3; // Margine sinistro colonna 2
    
    s_draw_text(s, c2, r++, COL_WHITE, "\u25BA SITUAZIONE CUCINA");
    r++;
    
    // DEFINIZIONE OFFSETS COLONNE (La soluzione al problema di allineamento)
    // Relative a c2
    int off_p = 0;  // Piatto
    int off_s = 14; // Serviti
    int off_r = 24; // Rimasti
    
    // Header Tabella
    s_draw_text(s, c2 + off_p, r, COL_GRAY, "PIATTO");
    s_draw_text(s, c2 + off_s, r, COL_GRAY, "SERVITI");
    s_draw_text(s, c2 + off_r, r, COL_GRAY, "RIMASTI");
    r++;
    
    // Linea sottile sotto header
    draw_hline(s, c2, r++, 32, COL_GRAY);

    const char* labels[] = {"Primi", "Secondi", "Caffe"};
    const int   types[]  = {FIRST_COURSE, MAIN_COURSE, COFFEE_BAR};

    it(i, 0, 3) {
        int t = types[i];
        size_t srv = st[t].stats.served_dishes;
        
        // Colonna 1: Nome
        s_draw_text(s, c2 + off_p, r, COL_WHITE, "%s", labels[i]);
        
        // Colonna 2: Serviti
        s_draw_text(s, c2 + off_s, r, COL_WHITE, "%zu", srv);
        
        // Colonna 3: Rimasti
        if (t == COFFEE_BAR) {
             s_draw_text(s, c2 + off_r, r, COL_WHITE, "\u221E"); // Simbolo Infinito
        } else {
            size_t leftovers = 0;
            it(k, 0, ctx->avl_dishes[t].size) leftovers += ctx->avl_dishes[t].data[k].quantity;
            
            // Cambio colore se scarseggia il cibo (< 10 porzioni)
            uint8_t col = (leftovers < 10) ? COL_WHITE : COL_GRAY; // O rosso se lo implementi
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
        int active =
            ctx->config.nof_wk_seats[i] - sem_getval(st[i].wk_data.sem);

        // RECUPERIAMO L'ARRAY DEI WORKER DALLA SHM
        // Questo ci permette di vedere lo stato del singolo processo
        const worker_t *wks = get_workers(st[i].wk_data.shmid);

        // Nome Stazione
        s_draw_text(s, c2, r, COL_GRAY, "%s:", st_names[i]);
        
        // Tempo Medio (Allineato)
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

    
    // --- 6. FOOTER ---
    s_draw_text(s, 2, H - 2, COL_GRAY, "Premi [q] per terminare la simulazione.");


    int box_w = 22;
    int box_x = mid_x + 2; // Margine destro
    int box_y = H - 2;         // Stessa riga del footer

    s_draw_text(s, box_x, box_y, COL_GRAY, "SYS:[");

    if (ctx->is_disorder_active) {
        // *** STATO DI DISORDER (FLATLINE con GLITCH) ***
        // Flatline con occasionali glitch
        const char* glitch_chars[] = {"▂", "▃", "▄"};
        for (int i = 0; i < 15; i++) {
            // Glitch casuale ogni tanto
            if ((min + i) % 7 == 0 && i % 3 == 0) {
                s_draw_text(s, box_x + 5 + i, box_y, COL_RED, "%s", glitch_chars[i % 3]);
            } else {
                s_draw_text(s, box_x + 5 + i, box_y, COL_RED, "▁");
            }
        }
        
        // Lampeggio più aggressivo: alterna ogni 2 update (invece di 3)
        // e usa diversi stati di visibilità
        int blink_state = (min / 2) % 4;
        if (blink_state == 0 || blink_state == 2) {
            s_draw_text(s, box_x + box_w + 2, box_y, COL_RED, "!ALERT!");
        } else if (blink_state == 1) {
            s_draw_text(s, box_x + box_w + 2, box_y, COL_GRAY, "!ALERT!");
        }
        // Sul 4° stato (blink_state == 3) non disegna nulla = spazio vuoto

    } else {
        // *** STATO NORMALE (ONDA SINUSOIDALE) ***
        // Usa caratteri Unicode per creare un'onda fluida e continua
        const char* wave[] = {
            "▁", "▂", "▃", "▄", "▅", "▆", "▇", "█",  // Salita
            "▇", "▆", "▅", "▄", "▃", "▂"              // Discesa
        };
        const int wave_count = 14;
        const int view_w = 16;

        // Crea effetto onda scorrevole moltiplicando per velocità
        for (int i = 0; i < view_w; i++) {
            int idx = ((min * 2) + i) % wave_count;  // Velocità x2
            s_draw_text(s, box_x + 5 + i, box_y, COL_GREEN, "%s", wave[idx]);
        }
    }
    s_draw_text(s, box_x + 21, box_y, COL_GRAY, "]");

        
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
    fprintf(f, "%zu, %zu\n", ctx_shm, st_shm);
    fclose(f);
}

void
reset_shared_data() {
    FILE *f = zfopen("data/shared", "w");
    fprintf(f, "%d, %d\n", -1, -1);
    fclose(f);
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
