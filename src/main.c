#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <sys/msg.h>
#include <sys/sem.h>
#include <sys/shm.h>
#include <sys/wait.h>

#include "config.h"
#include "const.h"
#include "menu.h"
#include "objects.h"
#include "tools.h"
#include "tui.h"

/* =================================================================
 * Global State & Signal Flags
 * ================================================================= */

static int g_priority_list[4];

/**
 * @brief Global structure to track client process IDs for cleanup.
 */
static struct {
    pid_t *id;
    size_t cnt;
} g_client_pids;

volatile sig_atomic_t g_users_req =
    0; /**< Flag set by SIGUSR2 for dynamic user addition. */
volatile sig_atomic_t g_stop_req =
    0; /**< Flag set by SIGINT/SIGTERM for graceful shutdown. */
static shmid_t g_shmid =
    0; /**< Shared memory ID used to pass context to clients. */

/* =================================================================
 * Function Prototypes
 * ================================================================= */

/* Core Simulation Flow */
void
sim_day(simctx_t *ctx, station *st, size_t day, screen *s, bool *manual_quit);
int  process_new_users(simctx_t *ctx);
void assign_roles(const simctx_t *ctx, station *st);

/* System Initialization & Teardown */
simctx_t *init_ctx(size_t shm_id, conf_t conf);
void      release_ctx(shmid_t shmid, simctx_t *ctx);
station  *init_stations(simctx_t *ctx, size_t shmid);
void      release_station(station st);
void      write_shared_data(shmid_t ctx_shm, shmid_t st_shm);
void      reset_shared_data();

/* Process Management */
void init_groups(simctx_t *ctx, const shmid_t ctx_shm);
void init_client(const shmid_t ctx_id, const size_t group_idx);
void init_worker(
    const shmid_t ctx_id,
    const shmid_t st_id,
    const size_t  idx,
    const loc_t   role
);
void release_clients(simctx_t *ctx);
void kill_all_child(simctx_t *ctx, station *stations, int sig);

/* UI / TUI Components */
screen *init_scr();
void    kill_scr(screen *s);
void    render_dashboard(
       screen   *s,
       simctx_t *ctx,
       station  *st,
       size_t    day,
       size_t    min,
       int       new_clients
   );
void render_final_report(screen *s, simctx_t *ctx, station *st, bool stopped);

/* Signal Handlers */
void handler_new_users(int sig);
void handler_stop(int sig);

/* =================================================================
 * Main Entry Point
 * ================================================================= */

/**
 * @brief Entry point of the simulation. Initializes IPC, TUI, and enters the
 * main loop.
 */
int
main(int argc, char **argv) {
    conf_t conf = {};

    /* Argument parsing: load default or provided configuration file */
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
        return -1;
    }

    /* Clear legacy logs/data files */
    fclear("data/simulation.log");
    fclear("data/stats.csv");

    /* Initialize randomness and UI */
    signal(SIGUSR1, SIG_IGN);
    srand((unsigned int)time(NULL));
    screen *screen = init_scr();

    /* Setup Signal Handlers for termination */
    struct sigaction sa_stop;
    memset(&sa_stop, 0, sizeof(sa_stop));
    sa_stop.sa_handler = handler_stop;
    sigaction(SIGINT, &sa_stop, NULL);
    sigaction(SIGTERM, &sa_stop, NULL);

    /* Memory Allocation & Context Setup */
    const size_t ctx_shm =
        zshmget(sizeof(simctx_t) + (MAX_TOTAL_USERS * sizeof(struct groups_t)));
    simctx_t *ctx = init_ctx(ctx_shm, conf);

    const size_t st_shm   = zshmget(sizeof(station) * NOF_STATIONS);
    station     *stations = init_stations(ctx, st_shm);

    /* Export IPC IDs to file for external tool synchronization */
    write_shared_data(ctx_shm, st_shm);

    /* Client Management Init */
    g_client_pids.id  = zcalloc(ctx->config.nof_users, sizeof(pid_t));
    g_client_pids.cnt = 0;

    /* Spawning Processes */
    init_groups(ctx, ctx_shm);
    assign_roles(ctx, stations);
    it(type, 0, NOF_STATIONS) {
        const size_t cap = stations[type].wk_data.cap;
        it(k, 0, cap) init_worker(ctx_shm, st_shm, k, (loc_t)type);
    }

    /* Simulation Loop: Iterates over simulation days */
    bool manual_quit;
    it(i, 0, ctx->config.sim_duration) {
        if (!ctx->is_sim_running)
            break;
        sim_day(ctx, stations, i, screen, &manual_quit);
    }

    /* Teardown Phase */
    ctx->is_sim_running = false;
    ctx->is_day_running = false;

    zprintf(ctx->sem[out], "MAIN: Simulation ended\n");
    render_final_report(screen, ctx, stations, manual_quit);

    reset_shared_data();
    it(i, 0, NOF_STATIONS) release_station(stations[i]);

    shmdt(stations);
    shm_kill(st_shm);
    release_clients(ctx);

    /* Synchronization: Wait for all children to exit */
    sem_set(ctx->sem[wall], ctx->config.nof_workers + ctx->config.nof_users);
    while (wait(NULL) > 0)
        ;

    release_ctx(ctx_shm, ctx);
    kill_scr(screen);
    return 0;
}

/* =================================================================
 * Core Logic Functions
 * ================================================================= */

/**
 * @brief Orchestrates a single working day in the simulation.
 * @param ctx Pointer to the shared simulation context.
 * @param stations Array of service stations.
 * @param day The current day index.
 * @param s Pointer to the TUI screen.
 * @param manual_quit Output flag to indicate if the user requested a quit.
 */
void
sim_day(
    simctx_t *ctx, station *stations, size_t day, screen *s, bool *manual_quit
) {
    if (day > 0)
        assign_roles(ctx, stations);

    zprintf(ctx->sem[out], "MAIN: Day started\n");

    /* Sync Start of Day */
    sem_wait(ctx->sem[shm]);
    ctx->is_day_running = true;
    sem_signal(ctx->sem[shm]);

    /* Reset barriers for the day */
    sem_set(ctx->sem[wall], ctx->config.nof_workers + ctx->config.nof_users);
    sem_set(ctx->sem[wk_end], ctx->config.nof_workers);
    sem_set(ctx->sem[cl_end], ctx->config.nof_users);

    size_t current_min = 0;
    size_t next_refill_min =
        get_service_time(ctx->config.avg_refill_time, var_srvc[4]);

    *manual_quit = false;

    /* The Minute-by-Minute Loop */
    while (ctx->is_sim_running && current_min < WORK_DAY_MINUTES) {
        int n_new = process_new_users(ctx);

        if (n_new > 0)
            sem_set(ctx->sem[cl_end], ctx->config.nof_users);

        if (current_min % DASHBOARD_UPDATE_RATE == 0 || n_new > 0)
            render_dashboard(s, ctx, stations, day + 1, current_min, n_new);

        /* Handle user input and external signals */
        char c = s_getch();
        if (c == 'q' || g_stop_req) {
            ctx->is_sim_running = false;
            *manual_quit        = true;
            g_stop_req          = 0;
            break;
        }

        znsleep(1); // Simulation tick
        current_min++;

        /* Handle Periodic Refill of food items */
        if (current_min >= next_refill_min) {
            sem_wait(ctx->sem[shm]);
            it(loc_idx, 0, 2) { // Only for first and main courses
                dish_avl_t  *elem_avl = ctx->avl_dishes[loc_idx].data;
                const size_t size_avl = ctx->avl_dishes[loc_idx].size;
                const size_t max      = ctx->config.max_porzioni[loc_idx];
                const size_t refill   = ctx->config.avg_refill[loc_idx];
                it(j, 0, size_avl) {
                    size_t *qty = &elem_avl[j].quantity;
                    *qty += refill;
                    if (*qty > max)
                        *qty = max;
                }
            }
            sem_signal(ctx->sem[shm]);
            size_t interval =
                get_service_time(ctx->config.avg_refill_time, var_srvc[4]);
            next_refill_min = current_min + interval;
        }
    }

    zprintf(ctx->sem[out], "MAIN: Day ended\n");

    /* Statistics Update */
    const int users_inside = sem_getval(ctx->sem[cl_end]);
    if (users_inside > 0)
        ctx->global_stats.users_not_served += users_inside;

    /* Stop simulation if crowd exceeds threshold */
    if (!(*manual_quit) && users_inside >= ctx->config.overload_threshold) {
        zprintf(
            ctx->sem[out], "MAIN: Sim end overload (Users left: %d)\n",
            users_inside
        );
        ctx->is_sim_running = false;
    }

    /* Accumulate day stats into global/total stats */
    it(i, 0, NOF_STATIONS) {
        ctx->global_stats.served_dishes += stations[i].stats.served_dishes;
        ctx->global_stats.earnings += stations[i].stats.earnings;
        ctx->global_stats.total_breaks += stations[i].stats.total_breaks;
        ctx->global_stats.worked_time += stations[i].stats.worked_time;

        stations[i].total_stats.served_dishes +=
            stations[i].stats.served_dishes;
        stations[i].total_stats.earnings += stations[i].stats.earnings;
        stations[i].total_stats.total_breaks += stations[i].stats.total_breaks;
        stations[i].total_stats.worked_time += stations[i].stats.worked_time;
    }

    save_stats_csv(ctx, stations, day);
    it(i, 0, NOF_STATIONS) { memset(&stations[i].stats, 0, sizeof(stats)); }

    ctx->is_day_running = false;

    /* Clean up children for next day or full exit */
    if (*manual_quit || !ctx->is_sim_running) {
        kill_all_child(ctx, stations, SIGTERM);
    } else {
        kill_all_child(ctx, stations, SIGUSR1);
        zprintf(ctx->sem[out], "MAIN: Reset day semaphores\n");
        sem_wait_zero(ctx->sem[wk_end]);
        sem_wait_zero(ctx->sem[cl_end]);
    }
}

/**
 * @brief Logic to distribute total workers among stations based on their
 * average service time weights.
 * * The algorithm ensures at least 1 worker per station, then distributes the
 * remaining workers proportionally to how slow the station is (weighted by
 * avg_srvc).
 */
void
assign_roles(const simctx_t *ctx, station *st) {
    it(i, 0, NOF_STATIONS) st[i].wk_data.cap = 0;

    const int total_workers = ctx->config.nof_workers;

    /* Ensure baseline coverage: 1 worker per station */
    it(i, 0, NOF_STATIONS) { st[i].wk_data.cap = 1; }

    int remaining = total_workers - NOF_STATIONS;
    if (remaining <= 0)
        return;

    size_t total_srvc_time = 0;
    it(i, 0, NOF_STATIONS) { total_srvc_time += ctx->config.avg_srvc[i]; }

    if (total_srvc_time == 0)
        total_srvc_time = 1;
    int assigned_count = 0;

    /* Proportional distribution */
    it(type, 0, NOF_STATIONS) {
        size_t weight        = ctx->config.avg_srvc[type];
        int    extra_workers = (int)((weight * remaining) / total_srvc_time);
        st[type].wk_data.cap += extra_workers;
        assigned_count += extra_workers;
    }

    /* Assign remaining workers based on priority list */
    int leftovers = remaining - assigned_count;
    int p_idx     = 0;

    while (leftovers > 0) {
        const loc_t target = g_priority_list[p_idx];
        st[target].wk_data.cap++;
        leftovers--;
        p_idx = (p_idx + 1) % NOF_STATIONS;
    }
}

/**
 * @brief Checks if a signal was received to add new users and forks new client
 * processes.
 * @return Number of new users added.
 */
int
process_new_users(simctx_t *ctx) {
    sem_wait(ctx->sem[shm]);
    int num_new = ctx->added_users;
    ctx->added_users = 0;    
    sem_signal(ctx->sem[shm]);
    
    if (ctx->config.nof_users + num_new > MAX_TOTAL_USERS) {
        zprintf(ctx->sem[out], "[ERROR] Max users limit reached in SHM!\n");
        return 0;
    }

    if (num_new <= 0)
        return 0;

    zprintf(
        ctx->sem[out], "[MAIN] Request received for %d new users!\n", num_new
    );

    size_t old_count = g_client_pids.cnt;
    size_t new_total = old_count + num_new;
    pid_t *new_ptr   = zrealloc(g_client_pids.id, new_total * sizeof(pid_t));
    g_client_pids.id = new_ptr;
    
    it(i, 0, num_new) {
        size_t new_idx = old_count + i;
        
        ctx->groups[new_idx].id = new_idx;
        ctx->groups[new_idx].total_members = 1;
        ctx->groups[new_idx].members_ready = 0;
        ctx->groups[new_idx].sem = sem_init(0);

        init_client(g_shmid, new_idx);
    }

    sem_wait(ctx->sem[shm]);
    ctx->config.nof_users += num_new;
    sem_signal(ctx->sem[shm]);

    return num_new;
}

/* =================================================================
 * System & Process Functions
 * ================================================================= */

/**
 * @brief Initializes the main simulation context in shared memory.
 */
simctx_t *
init_ctx(const size_t shm_id, const conf_t conf) {
    simctx_t *ctx = get_ctx(shm_id);

    memset(ctx, 0, sizeof(simctx_t));
    ctx->is_sim_running = true;
    ctx->config         = conf;

    it(i, 0, NOF_STATIONS) ctx->id_msg_q[i] =
        zmsgget(IPC_PRIVATE, IPC_CREAT | SHM_RW);

    load_menu("data/menu.json", ctx);

    /* Initialize food availability */
    it(loc, 0, 3) {
        struct available_dishes *dishes = &ctx->avl_dishes[loc];
        it(i, 0, ctx->menu[loc].size) {
            dishes->data[i].id = ctx->menu[loc].data[i].id;
            if (loc < COFFEE)
                dishes->data[i].quantity = ctx->config.avg_refill[loc];
            else
                dishes->data[i].quantity = 99999; // Unlimited coffee
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

    /* Sort stations by service time to establish allocation priority */
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

/**
 * @brief Destroys IPC resources and detaches context.
 */
void
release_ctx(shmid_t shmid, simctx_t *ctx) {
    it(i, 0, SEM_CNT) sem_kill(ctx->sem[i]);
    it(i, 0, ctx->config.nof_users) sem_kill(ctx->groups[i].sem);
    it(i, 0, NOF_STATIONS) msg_kill((int)ctx->id_msg_q[i]);
    shmdt(ctx);
    shm_kill(shmid);
}

/**
 * @brief Initializes station objects and their respective shared memory for
 * worker data.
 */
station *
init_stations(simctx_t *ctx, size_t shmid) {
    station *st = get_stations(shmid);
    memset(st, 0, sizeof(station) * NOF_STATIONS);

    it(i, 0, NOF_STATIONS) {
        st[i].type        = (loc_t)i;
        st[i].sem         = sem_init(1);
        st[i].wk_data.sem = sem_init(ctx->config.nof_wk_seats[i]);
        st[i].wk_data.shmid =
            zshmget(sizeof(worker_t) * ctx->config.nof_workers);

        if (i < CHECKOUT) {
            it(j, 0, ctx->menu[i].size) st[i].menu[j] = ctx->menu[i].data[j];
        }
    }
    return st;
}

/**
 * @brief Cleanup for a specific station.
 */
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

/**
 * @brief Forks a new worker process.
 */
void
init_worker(
    const shmid_t ctx_id,
    const shmid_t st_id,
    const size_t  idx,
    const loc_t   role
) {
    const pid_t pid = zfork();
    if (pid == 0) {
        char *args[] = {"worker",       itos((int)ctx_id), itos((int)st_id),
                        itos((int)idx), itos(role),        NULL};
        execve("./bin/worker", args, NULL);
        panic("ERROR: Execve failed launching a worker\n");
    }
}

/**
 * @brief Divides total users into randomly sized groups and forks client
 * processes.
 */
void
init_groups(simctx_t *ctx, const shmid_t ctx_shm) {
    size_t users_remaining       = ctx->config.nof_users;
    size_t group_idx             = 0;
    size_t members_current_group = 0;
    size_t target_size           = 0;

    it(i, 0, ctx->config.nof_users) {
        if (members_current_group == 0) {
            target_size = (rand() % ctx->config.max_users_per_group) + 1;
            if (target_size > users_remaining)
                target_size = users_remaining;

            ctx->groups[group_idx].id            = group_idx;
            ctx->groups[group_idx].total_members = target_size;
            ctx->groups[group_idx].members_ready = 0;
            ctx->groups[group_idx].sem           = sem_init(0);
            members_current_group                = target_size;
        }

        init_client(ctx_shm, group_idx);
        members_current_group--;
        users_remaining--;
        if (members_current_group == 0)
            group_idx++;
    }
}

/**
 * @brief Forks a new client process.
 */
void
init_client(const shmid_t ctx_id, const size_t group_idx) {
    const pid_t pid         = zfork();
    char       *ticket_attr = (rand() % 100 < 80) ? "1" : "0";

    if (pid == 0) {
        char *args[] = {
            "client", ticket_attr, itos((int)ctx_id), itos((int)group_idx), NULL
        };
        execve("./bin/client", args, NULL);
        panic("ERROR: Execve failed for client\n");
    }
    g_client_pids.id[g_client_pids.cnt++] = pid;
}

/**
 * @brief Terminates all active client processes.
 */
void
release_clients(simctx_t *ctx) {
    it(i, 0, ctx->config.nof_users) kill(g_client_pids.id[i], SIGTERM);
}

/**
 * @brief Sends a specific signal to all child processes (workers and clients).
 */
void
kill_all_child(simctx_t *ctx, station *stations, int sig) {
    it(i, 0, NOF_STATIONS) {
        const worker_t *wks = get_workers(stations[i].wk_data.shmid);
        it(j, 0, stations[i].wk_data.cap) { kill(wks[j].pid, sig); }
    }
    it(i, 0, ctx->config.nof_users) { kill(g_client_pids.id[i], sig); }
}

/**
 * @brief Exports essential shared memory IDs to disk and sets up signal
 * handlers.
 */
void
write_shared_data(shmid_t ctx_shm, shmid_t st_shm) {
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

/**
 * @brief Resets shared data files to a null/empty state.
 */
void
reset_shared_data() {
    FILE *f = zfopen("data/shared", "w");
    fprintf(f, "%d, %d\n", -1, -1);
    fclose(f);

    FILE *f_pid = zfopen("data/main.pid", "w");
    fprintf(f_pid, "%d", -1);
    fclose(f_pid);
}

/* =================================================================
 * UI Rendering Functions
 * ================================================================= */

/**
 * @brief Renders the real-time simulation dashboard.
 */
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
    const size_t W     = s->cols;
    const size_t H     = s->rows;
    const size_t mid_x = W / 2;

    static int notification_timer = 0;
    static int saved_user_count   = 0;

    if (new_clients > 0) {
        notification_timer = TUI_NOTIFICATIONS_LEN;
        saved_user_count   = new_clients;
    }

    draw_box(s, 0, 0, W, H, COL_GRAY);
    const char *title = " OASI DEL GOLFO - DASHBOARD ";
    s_draw_text(s, (W - strlen(title)) / 2, 0, COL_WHITE, title);

    draw_hline(s, 1, 2, W - 2, COL_GRAY);
    s_draw_text(s, 0, 2, COL_GRAY, BOX_VR);
    s_draw_text(s, W - 1, 2, COL_GRAY, BOX_VL);
    s_draw_text(s, mid_x, 2, COL_GRAY, BOX_HD);

    /* Header Info */
    s_draw_text(s, 2, 1, COL_GRAY, "DAY: ");
    s_draw_text(s, 10, 1, COL_WHITE, "%zu/%d", day, ctx->config.sim_duration);

    int u_total    = ctx->config.nof_users;
    int u_finished = sem_getval(ctx->sem[cl_end]);
    int u_inside   = u_total - u_finished;

    if (u_total > 0) {
        int   bar_w = 20;
        float pct   = (float)u_inside / (float)u_total;
        char  count_buf[32];
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
    s_draw_text(s, mid_x, H - 1, COL_GRAY, BOX_HU);

    /* Left Column: Flow Statistics */
    size_t r  = 4;
    size_t c1 = 2;
    s_draw_text(s, c1, r++, COL_WHITE, "\u25BA FLOW STATISTICS");
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
    s_draw_text(s, c1 + 2, r, COL_GRAY, "Total Served:");
    s_draw_text(s, c1 + 2 + label_w, r++, COL_WHITE, "%zu", tot_served);
    s_draw_text(s, c1 + 2, r, COL_GRAY, "Not Served:");
    s_draw_text(s, c1 + 2 + label_w, r++, COL_WHITE, "%zu", tot_not_srv);
    s_draw_text(s, c1 + 2, r, COL_GRAY, "Seated at Tables:");
    s_draw_text(s, c1 + 2 + label_w, r++, COL_WHITE, "%d", u_eating);

    r += 2;
    s_draw_text(s, c1, r++, COL_WHITE, "\u25BA ECONOMY & STAFF");
    r++;

    size_t revenue = st[CHECKOUT].stats.earnings;
    s_draw_text(s, c1 + 2, r, COL_GRAY, "Revenue:");
    s_draw_text(s, c1 + 2 + label_w, r++, COL_WHITE, "%zu€", revenue);
    s_draw_text(s, c1 + 2, r, COL_GRAY, "Total Breaks:");
    s_draw_text(s, c1 + 2 + label_w, r++, COL_WHITE, "%zu", tot_breaks);

    /* Right Column: Kitchen & Efficiency */
    r         = 4;
    size_t c2 = mid_x + 3;
    s_draw_text(s, c2, r++, COL_WHITE, "\u25BA KITCHEN STATUS");
    r++;

    int off_p = 0;
    int off_s = 14;
    int off_r = 24;
    s_draw_text(s, c2 + off_p, r, COL_GRAY, "DISH");
    s_draw_text(s, c2 + off_s, r, COL_GRAY, "SERVED");
    s_draw_text(s, c2 + off_r, r, COL_GRAY, "LEFT");
    r++;
    draw_hline(s, c2, r++, 32, COL_GRAY);

    const char *labels[] = {"First", "Main", "Coffee"};
    const int   types[]  = {FIRST_COURSE, MAIN_COURSE, COFFEE_BAR};

    it(i, 0, 3) {
        int t = types[i];
        s_draw_text(s, c2 + off_p, r, COL_WHITE, "%s", labels[i]);
        s_draw_text(
            s, c2 + off_s, r, COL_WHITE, "%zu", st[t].stats.served_dishes
        );
        if (t == COFFEE_BAR) {
            s_draw_text(s, c2 + off_r, r, COL_WHITE, "\u221E");
        } else {
            size_t leftovers = 0;
            it(k, 0, ctx->avl_dishes[t].size) leftovers +=
                ctx->avl_dishes[t].data[k].quantity;
            uint8_t col = (leftovers < 10) ? COL_WHITE : COL_GRAY;
            s_draw_text(s, c2 + off_r, r, col, "%zu", leftovers);
        }
        r++;
    }

    r += 2;
    s_draw_text(s, c2, r++, COL_WHITE, "\u25BA STATION EFFICIENCY");
    r++;

    const char *st_names[] = {"First", "Main", "Coffee", "Check"};
    it(i, 0, NOF_STATIONS) {
        float avg =
            st[i].stats.served_dishes > 0
                ? (float)st[i].stats.worked_time / st[i].stats.served_dishes
                : 0.0f;
        int cap = (int)st[i].wk_data.cap;
        int active =
            ctx->config.nof_wk_seats[i] - sem_getval(st[i].wk_data.sem);
        const worker_t *wks = get_workers(st[i].wk_data.shmid);

        s_draw_text(s, c2, r, COL_GRAY, "%s:", st_names[i]);
        s_draw_text(s, c2 + 7, r, COL_WHITE, "%4.0fns", avg);
        s_draw_text(s, c2 + 17, r, COL_WHITE, "%02d/%02d", active, cap);

        int bar_x = c2 + 22;
        s_draw_text(s, bar_x, r, COL_GRAY, "[");
        for (int k = 0; k < cap; k++) {
            if (wks[k].paused)
                s_draw_text(s, bar_x + 1 + k, r, COL_GRAY, "_");
            else
                s_draw_text(s, bar_x + 1 + k, r, COL_WHITE, "\u25A0");
        }
        s_draw_text(s, bar_x + 1 + cap, r, COL_GRAY, "]");
        r++;
    }

    /* Footer & System Alerts */
    int footer_y = s->rows - 4;
    if (notification_timer > 0) {
        s_draw_text(
            s, 2, footer_y, COL_GREEN, "!!! %d NEW CLIENTS ARRIVED !!!",
            saved_user_count
        );
        notification_timer--;
    }
    s_draw_text(s, 2, H - 2, COL_GRAY, "Press [q] to terminate simulation.");

    /* Disorder / Glitch UI Effect */
    int box_x = mid_x + 2;
    int box_y = H - 2;
    s_draw_text(s, box_x, box_y, COL_GRAY, "SYS:[");
    if (ctx->is_disorder_active) {
        const char *glitch_chars[] = {"▂", "▃", "▄"};
        for (int i = 0; i < 15; i++) {
            if ((min + i) % 7 == 0 && i % 3 == 0)
                s_draw_text(
                    s, box_x + 5 + i, box_y, COL_RED, "%s", glitch_chars[i % 3]
                );
            else
                s_draw_text(s, box_x + 5 + i, box_y, COL_RED, "▁");
        }
        int blink_state = (min / 2) % 4;
        if (blink_state == 0 || blink_state == 2)
            s_draw_text(s, box_x + 24, box_y, COL_RED, "!ALERT!");
    } else {
        const char *pattern[] = {"▂", "▂", "▃", "▃", "▄", "▄", "▅", "▅",
                                 "▆", "▆", "▇", "▇", "▇", "▆", "▆", "▅",
                                 "▅", "▄", "▄", "▃", "▃", "▂", "▂"};
        for (int i = 0; i < 16; i++) {
            int idx = ((min * 2) + i) % 23;
            s_draw_text(s, box_x + 5 + i, box_y, COL_GREEN, "%s", pattern[idx]);
        }
    }
    s_draw_text(s, box_x + 21, box_y, COL_GRAY, "]");

    s_draw_text(s, W - 1, 0, COL_GRAY, BOX_TR);
    s_draw_text(s, W - 1, H - 1, COL_GRAY, BOX_BR);
    s_display(s);
}

/**
 * @brief Displays a summary report after the simulation ends.
 */
void
render_final_report(screen *s, simctx_t *ctx, station *st, bool manual_quit) {
    const int users_finished = sem_getval(ctx->sem[cl_end]);
    const int limit_users    = ctx->config.overload_threshold;

    const bool is_disorder = ctx->is_disorder_active;
    const bool is_overload = !manual_quit && (users_finished >= limit_users);
    const bool is_timeout  = !manual_quit && !is_overload && !is_disorder;

    /* Snapshot current leftovers */
    size_t left_primi = 0;
    it(k, 0, ctx->avl_dishes[FIRST_COURSE].size) left_primi +=
        ctx->avl_dishes[FIRST_COURSE].data[k].quantity;
    size_t left_secondi = 0;
    it(k, 0, ctx->avl_dishes[MAIN_COURSE].size) left_secondi +=
        ctx->avl_dishes[MAIN_COURSE].data[k].quantity;

    const size_t tot_primi    = st[FIRST_COURSE].total_stats.served_dishes;
    const size_t tot_secondi  = st[MAIN_COURSE].total_stats.served_dishes;
    const size_t tot_caffe    = st[COFFEE_BAR].total_stats.served_dishes;
    const size_t tot_dishes   = ctx->global_stats.served_dishes;
    const size_t tot_earn     = ctx->global_stats.earnings;
    const size_t tot_breaks   = ctx->global_stats.total_breaks;
    const size_t tot_unserved = ctx->global_stats.users_not_served;

    int         status_col;
    const char *title_status;
    char        reason[128];

    if (manual_quit) {
        status_col   = COL_GRAY;
        title_status = "SIMULATION INTERRUPTED";
        snprintf(
            reason, sizeof(reason), "REASON: Manual Interruption (Q / CTRL+C)"
        );
    } else if (is_disorder) {
        status_col   = COL_RED;
        title_status = "TERMINATION: COM. DISORDER";
        snprintf(
            reason, sizeof(reason),
            "REASON: Checkout station blocked (Communication Disorder)"
        );
    } else if (is_overload) {
        status_col   = COL_RED;
        title_status = "TERMINATION: OVERLOAD";
        snprintf(
            reason, sizeof(reason),
            "REASON: Waiting users (%d) > Threshold (%d)", users_finished,
            limit_users
        );
    } else {
        status_col   = COL_GREEN;
        title_status = "TERMINATION: TIMEOUT";
        snprintf(
            reason, sizeof(reason), "REASON: Reached max duration (%d days)",
            ctx->config.sim_duration
        );
    }

    while (true) {
        s_clear(s);
        const int W     = s->cols;
        const int H     = s->rows;
        const int mid_x = W / 2;

        draw_box(s, 0, 0, W, H, COL_WHITE);
        draw_box(s, 1, 1, W - 2, 5, status_col);
        s_draw_text(
            s, (W - strlen(title_status)) / 2, 2, COL_WHITE, "%s", title_status
        );
        s_draw_text(s, (W - strlen(reason)) / 2, 3, COL_WHITE, "%s", reason);

        int r     = 8;
        int c1    = 4;
        int val_x = c1 + 24;
        s_draw_text(s, c1, r++, COL_WHITE, "\u25BA GLOBAL STATISTICS");
        draw_hline(s, c1, r++, 35, COL_GRAY);
        s_draw_text(s, c1, r, COL_GRAY, "Total Earnings:");
        s_draw_text(s, val_x, r++, COL_GREEN, "%zu €", tot_earn);
        s_draw_text(s, c1, r, COL_GRAY, "Total Staff Breaks:");
        s_draw_text(s, val_x, r++, COL_WHITE, "%zu", tot_breaks);
        r++;
        s_draw_text(s, c1, r, COL_GRAY, "Total Unserved Users:");
        s_draw_text(
            s, val_x, r++, (tot_unserved > 0) ? COL_RED : COL_WHITE, "%zu",
            tot_unserved
        );
        s_draw_text(s, c1, r, COL_GRAY, "Total Served Dishes:");
        s_draw_text(s, val_x, r++, COL_WHITE, "%zu", tot_dishes);

        /* Detail Column */
        int c2 = mid_x + 2;
        r      = 8;
        s_draw_text(s, c2, r++, COL_WHITE, "\u25BA COURSES & LEFTOVERS");
        draw_hline(s, c2, r++, W - c2 - 4, COL_GRAY);
        s_draw_text(s, c2, r, COL_GRAY, "CATEGORY      SERVED      LEFTOVERS");
        r += 2;
        s_draw_text(
            s, c2, r, COL_WHITE, "First         %zu          %zu", tot_primi,
            left_primi
        );
        r++;
        s_draw_text(
            s, c2, r, COL_WHITE, "Main          %zu          %zu", tot_secondi,
            left_secondi
        );
        r++;
        s_draw_text(
            s, c2, r, COL_WHITE, "Coffee        %zu          \u221E", tot_caffe
        );

        int footer_y = H - 4;
        draw_hline(s, 1, footer_y, W - 2, COL_GRAY);
        s_draw_text(
            s, 4, footer_y + 1, COL_WHITE,
            "Press [q] to destroy IPC resources and exit."
        );
        s_display(s);

        char c = s_getch();
        if (c == 'q' || g_stop_req)
            break;
        usleep(100000);
    }
}

/**
 * @brief TUI lifecycle: Initialization.
 */
screen *
init_scr() {
    size_t rows, cols;
    get_terminal_size(&rows, &cols);
    screen *s = init_screen(rows, cols);
    enableRawMode();
    return s;
}

/**
 * @brief TUI lifecycle: Cleanup.
 */
void
kill_scr(screen *s) {
    reset_terminal();
    free_screen(s);
}

/* =================================================================
 * Signal Handlers Implementation
 * ================================================================= */

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
