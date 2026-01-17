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
void init_worker  (simctx_t*, shmid_t, shmid_t, loc_t);
void sim_day      (simctx_t*, shmid_t, station*, shmid_t);
void assign_roles (simctx_t*, station*);
simctx_t* init_ctx(const size_t);

int _compare_pair_station(const void* a, const void* b) {
    const struct pair_station* first  = (const struct pair_station*)a;
    const struct pair_station* second = (const struct pair_station*)b;
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

    const size_t
    shm_stations_id = zshmget(
                          IPC_PRIVATE,
                          sizeof(station) * NOF_STATIONS,
                          IPC_CREAT | SHM_RW
                      );

    station *st = get_stations(shm_stations_id);
    memset(st, NOF_STATIONS, sizeof(station));
    it (i, 0, NOF_STATIONS) {
        st[i].type = (loc_t)i;
        st[i].wk_data.sem = sem_init(ctx->config.nof_wk_seats[i]);
        
        it (j, 0, ctx->menu[i].size) 
            st[i].menu[j] = ctx->menu[i].elements[j];

        
        // size_t nworkers = ctx->config.nof_wk_seats[i];
        // st[i].wk_data.shmid = zshmget(
        //                         IPC_PRIVATE,
        //                         sizeof(worker_t) * nworkers,
        //                         IPC_CREAT | SHM_RW
        //                       );
        

    }

    it (i, 0, ctx->config.nof_users)
        init_client(shm_id);
    
    it(i, 0, ctx->config.sim_duration)
        sim_day(ctx, shm_id, st, shm_stations_id);


    while(wait(NULL) > 0);

    shmctl(shm_id, IPC_RMID, NULL);
    semctl(ctx->sem.out, 0, IPC_RMID);
    semctl(ctx->sem.tbl, 0, IPC_RMID);
    semctl(ctx->sem.shm, 0, IPC_RMID);
    semctl(ctx->sem.day, 0, IPC_RMID);
    it(i, 0, NOF_STATIONS)
        msgctl(ctx->id_msg_q[i], IPC_RMID, NULL);

    return 0;
}

void sim_day(
    simctx_t* ctx,
    shmid_t   ctx_id,
    station*  stations,
    shmid_t   st_id
) {
    assign_roles(ctx, stations);

    it (type, 0, NOF_STATIONS) {
        int cnt = stations[type].wk_data.cap;

        it (k, 0, cnt)
            init_worker(ctx, ctx_id, st_id, (loc_t)type);
    }

    while (ctx->is_sim_running) {
        znsleep(600);

        if (!ctx->is_sim_running) break;

        const size_t avg_refill_time = ctx->config.avg_refill_time;
        const size_t actual_duration = get_service_time(
                                            avg_refill_time,
                                            var_srvc[4]
                                       );

        znsleep(actual_duration);
        
        sem_wait(ctx->sem.shm);

        it (loc_idx, 0, 2) {
            dish_available_t *elem_avl = ctx->available_dishes[loc_idx].elements;
            size_t            size_avl = ctx->available_dishes[loc_idx].size;
            size_t            max      = ctx->config.max_porzioni[loc_idx];
            size_t            refill   = ctx->config.avg_refill[loc_idx];
            
            it (j, 0, size_avl) {
                size_t* qty = &elem_avl[j].quantity;

                *qty += refill;
                if (*qty > max) *qty = max;
            }
        }
        sem_signal(ctx->sem.shm);
    }

}

/* ========================== PROCESSES ========================== */ 

void
init_worker (
    simctx_t* ctx,
    shmid_t   ctx_id,
    shmid_t   st_id,
    loc_t     role
) {
    const pid_t pid = zfork();

    if (pid == 0) {
        char *args[] = {
            "worker",
            itos((int)ctx_id),
            itos((int)st_id),
            itos((int)role),
            NULL 
        };

        execve("./bin/worker", args, NULL);
        panic("ERROR: Execve failed launching a worker\n");
    }
}

void init_client(
    shmid_t ctx_id
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

    // Inizializzazione piatti disponibili (Logica invariata)
    it (loc, 0, 3){
        struct available_dishes dishes = ctx->available_dishes[loc];
        it (i, 0, ctx->menu[loc].size) {
            dishes.elements[i].id = ctx->menu[loc].elements[i].id;
            dishes.elements[i].quantity = ctx->config.avg_refill[loc];
            dishes.size++;
        }
    }

    ctx->sem.out = sem_init(1);
    ctx->sem.shm = sem_init(1);
    ctx->sem.day = sem_init(1);
    ctx->sem.tbl = sem_init(ctx->config.nof_wk_seats[TABLE]);

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
    simctx_t* ctx,
    station* st
) {
    // the num_workers will never be less than 4
    // (the load_conf fun gives an error in that case)
    int num_workers = ctx->config.nof_workers;

    it (i, 0, NOF_STATIONS)
        st[i].wk_data.cap = 0;

    st[FIRST_COURSE].wk_data.cap++;
    st[MAIN_COURSE ].wk_data.cap++;
    st[COFFEE_BAR  ].wk_data.cap++;
    st[CHECKOUT    ].wk_data.cap++;
    
    it (i, 4, num_workers) {
        loc_t target_station_id = g_priority_list[i % 4];
        
        st[target_station_id].wk_data.cap++;
    }

    it (i, 0, NOF_STATIONS) {
        st[i].wk_data.shmid = zshmget(
                                IPC_PRIVATE,
                                sizeof(worker_t) * st[i].wk_data.cap,
                                IPC_CREAT | SHM_RW
                              );
    }
}
