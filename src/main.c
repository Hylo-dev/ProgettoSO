#include <stddef.h>
#include <stdio.h>
#include <string.h>
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

#define for_in(from, to) \
    for (size_t i = from; i < (size_t)to; i++)

#define SHM_RW 0666

/* Prototipi */
void init_client(char*, char*);
void init_worker(simctx_t*, char*, size_t, char*, char*);
void sim_day(simctx_t* ctx);
void assign_roles(simctx_t* ctx);
simctx_t* init_ctx(const size_t);

int _compare_pair_station(const void* a, const void* b) {
    const struct pair_station* first  = (const struct pair_station*)a;
    const struct pair_station* second = (const struct pair_station*)b;
    return (second->avg_time - first->avg_time);
}

inline const int 
sem_init() {
    const int sem = semget(IPC_PRIVATE, 1, IPC_CREAT | SHM_RW);
    semctl(sem, 0, SETVAL, 1);
    return sem;
}

int
main(void) {
    /* ========================== INIT ========================== */
    const int out_sem = sem_init();
    const int shm_sem = sem_init();
    
    srand((unsigned int)time(NULL));

    const size_t
    shm_id = zshmget(
        IPC_PRIVATE,
        sizeof(simctx_t),
        IPC_CREAT | SHM_RW
    );

    simctx_t* ctx = init_ctx(shm_id);

    char sshm_id [16];
    char sout_sem[16];
    char sshm_sem[16];

    sprintf(sshm_id,  "%zu", shm_id );
    sprintf(sout_sem, "%d" , out_sem);
    sprintf(sshm_sem, "%d" , shm_sem);

    for_in(0, ctx->config.nof_workers)
        init_worker(ctx, sshm_id, i, sout_sem, sshm_sem);

    for_in(0, ctx->config.nof_users)
        init_client(sshm_id, sout_sem);
    
    for_in(0, ctx->config.sim_duration)
        sim_day(ctx);

    while(wait(NULL) > 0);

    shmctl(shm_id, IPC_RMID, NULL);
    shmctl(ctx->shmid_roles, IPC_RMID, NULL);
    semctl(out_sem, 0, IPC_RMID);
    semctl(shm_sem, 0, IPC_RMID);
    for_in(0, NOF_STATIONS) msgctl(ctx->id_msg_q[i], IPC_RMID, NULL);

    return 0;
}

void sim_day(simctx_t* ctx) {
    assign_roles(ctx);
}

/* ========================== PROCESSES ========================== */ 

void init_client(
    char *shmid,
    char *zprintf_sem
) {
    const pid_t pid = zfork();

    if (pid == 0) {
        char *args[] = {
            "client",
            rand()%2 ? "1":"0",
            shmid,
            zprintf_sem,
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
    char*     shm_sem
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
    simctx_t* ctx = (simctx_t*)zshmat(shm_id, NULL, 0);

    memset(ctx, 0, sizeof(simctx_t));
    ctx->is_sim_running = true;

    load_config("data/config.json", &ctx->config);

    size_t roles_size = sizeof(worker_role_t) * ctx->config.nof_workers;
    
    ctx->shmid_roles = zshmget(IPC_PRIVATE, roles_size, IPC_CREAT | SHM_RW);
    
    // Attacchiamo il puntatore nel processo padre (Main)
    ctx->roles = (worker_role_t*)zshmat(ctx->shmid_roles, NULL, 0);
    
    // Azzeriamo la memoria dei ruoli
    memset(ctx->roles, 0, roles_size);

    for (size_t i = 0; i < NOF_STATIONS; i++) {
        ctx->id_msg_q[i] = zmsgget(IPC_PRIVATE, IPC_CREAT | SHM_RW);
    }

    // 6. Caricamento Menu
    load_menu("data/menu.json", ctx);

    // Inizializzazione piatti disponibili (Logica invariata)
    for (size_t i = 0; i < ctx->menu[MAIN].size; i++) {
        ctx->available_dishes[MAIN].elements[i].id = ctx->menu[MAIN].elements[i].id;
        ctx->available_dishes[MAIN].elements[i].quantity = 100; // O usa config.max_porzioni...
        ctx->available_dishes[MAIN].size++;
    }

    for (size_t i = 0; i < ctx->menu[FIRST].size; i++) {
        ctx->available_dishes[FIRST].elements[i].id = ctx->menu[FIRST].elements[i].id;
        ctx->available_dishes[FIRST].elements[i].quantity = 100;
        ctx->available_dishes[FIRST].size++;
    }

    for (size_t i = 0; i < ctx->menu[COFFEE].size; i++) {
        ctx->available_dishes[COFFEE].elements[i].id = ctx->menu[COFFEE].elements[i].id;
        ctx->available_dishes[COFFEE].elements[i].quantity = 100;
        ctx->available_dishes[COFFEE].size++;
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
        for(int i=0; i<num_workers; i++) roles_buffer[assigned_count++] = (location_t)i;
    }

    struct pair_station priority_list[4] = {
        { FIRST_COURSE, ctx->config.avg_srvc_primi },
        { MAIN_COURSE,  ctx->config.avg_srvc_main_course },
        { COFFEE_BAR,   ctx->config.avg_srvc_coffee },
        { CHECKOUT,     ctx->config.avg_srvc_cassa }
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
