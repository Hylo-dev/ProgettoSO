#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <sys/sem.h>
#include <time.h>
#include <stdlib.h> // Necessario per malloc/free

#include <unistd.h>
#include <sys/shm.h>
#include <sys/msg.h>
#include <sys/wait.h>

#include "config.h"
#include "const.h"
#include "objects.h"
#include "tools.h"
#include "menu.h"

#define SHM_RW 0666

/* Prototipi */
void init_client(char*, char*, char*);
void init_worker(simctx_t*, char*, size_t, char*, char*, char*);
void sim_day(simctx_t* ctx);
void assign_roles(simctx_t* ctx);
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

    const int out_sem = sem_init(1);
    const int shm_sem = sem_init(1);
    const int tbl_sem = sem_init(ctx->config.nof_wk_seats[TABLE]);

    const size_t
    shm_stations_id = zshmget(IPC_PRIVATE, sizeof(station) * NOF_STATIONS, IPC_CREAT | SHM_RW);

    station *st = get_stations(shm_stations_id);
    memset(st, NOF_STATIONS, sizeof(station));
    for (size_t i = 0; i < NOF_STATIONS; i++) {
        size_t nworkers = ctx->config.nof_wk_seats[i];
        st[i].wk_data.shmid = zshmget(
                                IPC_PRIVATE,
                                sizeof(worker_t) * nworkers,
                                IPC_CREAT | SHM_RW
                              );
        
        st[i].type = (location_t)i;

        for (size_t j = 0; j < ctx->menu[i].size; j++) 
            st[i].menu[j] = ctx->menu[i].elements[j];
    }

    char sshm_id [16];
    char sshm_st [16];
    char sout_sem[16];
    char sshm_sem[16];
    char stbl_sem[16];

    sprintf(sshm_id,  "%zu", shm_id );
    sprintf(sshm_st,  "%zu", shm_stations_id);
    sprintf(sout_sem, "%d" , out_sem);
    sprintf(sshm_sem, "%d" , shm_sem);
    sprintf(stbl_sem, "%d" , tbl_sem);

    it (i, 0, ctx->config.nof_workers)
        init_worker(ctx, sshm_id, i, sout_sem, sshm_sem, sshm_st);

    it (i, 0, ctx->config.nof_users)
        init_client(sshm_id, sout_sem, stbl_sem);
    
    it(i, 0, ctx->config.sim_duration)
        sim_day(ctx);

    while (ctx->is_sim_running) {

        znsleep(600);

        if (!ctx->is_sim_running) break;

        const size_t avg_refill_time = ctx->config.avg_refill_time;
        const size_t actual_duration = get_service_time(avg_refill_time, var_srvc[4]);
        znsleep(actual_duration);

        sem_wait(shm_sem);

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

        sem_signal(shm_sem);
    }


    while(wait(NULL) > 0);

    shmctl(shm_id, IPC_RMID, NULL);
    shmctl(ctx->shmid_roles, IPC_RMID, NULL);
    semctl(out_sem, 0, IPC_RMID);
    semctl(shm_sem, 0, IPC_RMID);
    it(i, 0, NOF_STATIONS)
        msgctl(ctx->id_msg_q[i], IPC_RMID, NULL);

    return 0;
}

void sim_day(simctx_t* ctx) {
    assign_roles(ctx);
}

/* ========================== PROCESSES ========================== */ 

void init_client(
    char *shmid,
    char *out_sem,
    char *tbl_sem
) {
    const pid_t pid = zfork();

    if (pid == 0) {
        char *args[] = {
            "client",
            rand()%2 ? "1":"0",
            shmid,
            out_sem,
            tbl_sem,
            NULL
        };
        execve("./bin/client", args, NULL);
        panic("ERROR: Execve failed for client\n");
    }
}

void init_worker(
    simctx_t* ctx,
    char*     shm_id,
    size_t    idx,
    char*     zprintf_sem,
    char*     shm_sem,
    char*     stations_id
) {
    const pid_t pid = zfork();

    if (pid == 0) {
        ctx->roles[idx].worker = getpid();

        char str_role_idx[16];
        sprintf(str_role_idx, "%zu", idx);
        
        char *args[] = {
            "worker",
            shm_id,
            str_role_idx,
            zprintf_sem,
            shm_sem,
            stations_id,
            NULL 
        };

        execve("./bin/worker", args, NULL);
        panic("ERROR: Execve failed for worker n. %zu\n", idx);
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

    size_t roles_size = sizeof(worker_role_t) * ctx->config.nof_workers;
    
    ctx->shmid_roles = zshmget(IPC_PRIVATE, roles_size, IPC_CREAT | SHM_RW);
    
    // Attacchiamo il puntatore nel processo padre (Main)
    ctx->roles = (worker_role_t*)zshmat(ctx->shmid_roles);
    
    // Azzeriamo la memoria dei ruoli
    memset(ctx->roles, 0, roles_size);

    for (size_t i = 0; i < NOF_STATIONS; i++) {
        ctx->id_msg_q[i] = zmsgget(IPC_PRIVATE, IPC_CREAT | SHM_RW);
    }

    // 6. Caricamento Menu
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

    assign_roles(ctx);

    return ctx;
}

void
assign_roles(
    simctx_t* ctx
) {
    int num_workers = ctx->config.nof_workers;
    
    location_t *roles_buffer = malloc(sizeof(location_t) * num_workers);
    if (!roles_buffer) panic("Malloc failed in assign_roles");

    int assigned_count = 0;

    if (num_workers >= 4) {
        roles_buffer[assigned_count++] = FIRST_COURSE; // 0
        roles_buffer[assigned_count++] = MAIN_COURSE;  // 1
        roles_buffer[assigned_count++] = COFFEE_BAR;   // 2
        roles_buffer[assigned_count++] = CHECKOUT;     // 3
        
    } else {
        it(i,0, num_workers)
            roles_buffer[assigned_count++] = (location_t)i;
    }

    struct pair_station priority_list[4] = {
        { FIRST_COURSE, ctx->config.avg_srvc[FIRST_COURSE] },
        { MAIN_COURSE,  ctx->config.avg_srvc[MAIN_COURSE]  },
        { COFFEE_BAR,   ctx->config.avg_srvc[COFFEE_BAR]   },
        { CHECKOUT,     ctx->config.avg_srvc[CHECKOUT]     }
    };

    qsort(priority_list, 4, sizeof(struct pair_station), _compare_pair_station);

    int p_index = 0;
    while (assigned_count < num_workers) {
        roles_buffer[assigned_count++] = priority_list[p_index].id;
        p_index++;
        if (p_index > 3) p_index = 0;
    }

    for (int i = num_workers - 1; i > 0; i--) {
        const int j = rand() % (i + 1);
        const location_t temp = roles_buffer[i];
        roles_buffer[i] = roles_buffer[j];
        roles_buffer[j] = temp;
    }

    for (int i = 0; i < num_workers; i++) {
        ctx->roles[i].role = roles_buffer[i];
    }

    free(roles_buffer);
}
