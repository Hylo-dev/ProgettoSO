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

#define SHM_RW 0666

// lista prioritaria per assegnare i lavoratori alle stazioni, calcolata una sola volta.
static int g_priority_list[4];

/* Prototipi */
void init_client  (shmid_t);
void init_worker  (const shmid_t, const shmid_t, const size_t, const loc_t);
void sim_day      (simctx_t *, station *, size_t);
void assign_roles (const simctx_t*, station*);
simctx_t* init_ctx(size_t);

int _compare_pair_station(const void* a, const void* b) {
    const struct pair_station* first  = a;
    const struct pair_station* second = b;
    return (second->avg_time - first->avg_time);
}

int
main(void) {
    /* ========================== INIT ========================== */
    srand((unsigned int)time(NULL));

    const size_t
    shm_id = zshmget(
        IPC_PRIVATE,
        sizeof(simctx_t),
        IPC_CREAT | SHM_RW
    );

    simctx_t* ctx = init_ctx(shm_id);

    printf("shm: %d\n",  ctx->sem.shm);
    printf("day: %d\n",  ctx->sem.wk_end);
    printf("out: %d\n",  ctx->sem.out);
    printf("tbl: %d\n",  ctx->sem.tbl);
    printf("wall: %d\n", ctx->sem.wall);
    printf("\n");

    const size_t
    shm_stations_id = zshmget(
                          IPC_PRIVATE,
                          sizeof(station) * NOF_STATIONS,
                          IPC_CREAT | SHM_RW
                      );

    station *st = get_stations(shm_stations_id);
    memset(st, 0, sizeof(station) * NOF_STATIONS);
    // In src/main.c dentro il main()

    it (i, 0, NOF_STATIONS) {
        st[i].type        = (loc_t)i;
        st[i].sem         = sem_init(1);
        st[i].wk_data.sem = sem_init(ctx->config.nof_wk_seats[i]);

        st[i].wk_data.shmid = zshmget(
            IPC_PRIVATE,
            sizeof(worker_t) * ctx->config.nof_workers,
            IPC_CREAT | SHM_RW
        );

        if (i < CHECKOUT) {
            it (j, 0, ctx->menu[i].size)
                st[i].menu[j] = ctx->menu[i].data[j];
        }
    }

    it (i, 0, ctx->config.nof_users)
        init_client(shm_id);

    assign_roles(ctx, st);
    it (type, 0, NOF_STATIONS) {
        const size_t cap = st[type].wk_data.cap;

        // the `k` index will be the index where the worker will put its data
        it (k, 0, cap)
            init_worker(shm_id, shm_stations_id, k, (loc_t)type);
    }
    
    it(i, 0, ctx->config.sim_duration)
        sim_day(ctx, st, i);

    ctx->is_sim_running = false;
    printf("MAIN: Fine sim\n");

    // Libera i wk così muoiono
    set_sem(ctx->sem.wall, ctx->config.nof_workers);
    while(wait(NULL) > 0);

    shmctl((int)shm_id, IPC_RMID, NULL);
    semctl(ctx->sem.out, 0, IPC_RMID);
    semctl(ctx->sem.tbl, 0, IPC_RMID);
    semctl(ctx->sem.shm, 0, IPC_RMID);
    semctl(ctx->sem.wk_end, 0, IPC_RMID);
    it(i, 0, NOF_STATIONS)
        msgctl((int)ctx->id_msg_q[i], IPC_RMID, NULL);

    return 0;
}

void sim_day(
          simctx_t *ctx,
          station  *stations,
    const size_t    day
) {
    if (day > 0) assign_roles(ctx, stations);

    printf("MAIN: Inizio giornata\n");
    ctx->is_day_running = true;

    // Settato per la quantita di wk attivi
    set_sem(ctx->sem.wk_end, ctx->config.nof_workers);
    set_sem(ctx->sem.wall,   ctx->config.nof_workers);

    size_t min = 0;
    const size_t avg_refill_time = ctx->config.avg_refill_time;
    while (ctx->is_sim_running && min < WORK_DAY_MINUTES) {
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
    printf("MAIN: Fine giornata\n");
    ctx->is_day_running = false;

    it(i, 0, NOF_STATIONS) {
        const worker_t *wks = get_workers(stations[i].wk_data.shmid);
        it(j, 0, stations[i].wk_data.cap) {
            if (wks[j].pid > 0) {
                kill(wks[j].pid, SIGUSR1);
            }
        }
    }

    printf("MAIN: Reset day sem\n");
    sem_wait_zero(ctx->sem.wk_end);

    printf("MAIN: Stats da implementare\n");
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

void init_client(const shmid_t ctx_id) {
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

    ctx->sem.out  = sem_init(1);
    ctx->sem.shm  = sem_init(1);
    ctx->sem.wk_end  = sem_init(0);
    ctx->sem.wall = sem_init(0);
    ctx->sem.tbl  = sem_init(ctx->config.nof_tbl_seats);

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
