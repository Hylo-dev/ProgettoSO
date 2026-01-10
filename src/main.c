#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include <unistd.h>
#include <sys/shm.h>
#include <sys/msg.h>
#include <sys/wait.h>

#include "const.h"
#include "objects.h"
#include "tools.h"
#include "menu.h"

#define SHM_RW 0666

void
init_client(char*);

void
init_worker(
    simctx_t*, char*, size_t
);

void
sim_day(simctx_t* ctx);

void
assign_roles(simctx_t* ctx);

int main(void) {
    
    srand((unsigned int)time(NULL));

    const size_t shm_id = zshmget(IPC_PRIVATE, sizeof(simctx_t), IPC_CREAT | SHM_RW);

    simctx_t* ctx = (simctx_t*)zshmat(shm_id, NULL, 0);

    // ----------------- STATIONS -----------------

    for (size_t i = 0; i < NOF_STATIONS; i++) {
        ctx->id_msg_q[i] = zmsgget(IPC_PRIVATE, IPC_CREAT | SHM_RW);
    }

    // ----------------- CLEAN SHM -----------------
    memset(ctx, 0, sizeof(simctx_t));
    ctx->is_sim_running = true;

    // ----------------- GET MENU -----------------
    load_menu("menu.json", ctx);

    for (size_t i = 0; i < ctx->main_menu_size; i++) {
        ctx->main_courses[i].id       = ctx->main_courses_menu[i].id;
        ctx->main_courses[i].quantity = 100;
    }

    for (size_t i = 0; i < ctx->first_menu_size; i++) {
        ctx->first_courses[i].id       = ctx->first_courses_menu[i].id;
        ctx->first_courses[i].quantity = 100;
    }

    for (size_t i = 0; i < ctx->coffee_menu_size; i++) {
        ctx->coffee_dishes[i].id       = ctx->coffee_menu[i].id;
        ctx->coffee_dishes[i].quantity = 100;
    }

    // CREATE WORKERS
    char str_shm_id[16];

    sprintf(str_shm_id, "%zu", shm_id);
    for (size_t i = 0; i < NOF_WORKERS; i++) {
        init_worker(ctx, str_shm_id, i);
    }

    sprintf(str_shm_id, "%zu", ctx->id_msg_q[0]);
    for (size_t i = 0; i < NOF_USERS; i++){
        init_client(str_shm_id);
    }

    for (size_t i = 0; i < SIM_DURATION; i++) {
        sim_day(ctx);
    }
}

void
init_client(
    char *first_msgq_id
) {
    const pid_t pid = zfork();

    if (pid == 0) {
        char *args[] = { "client", first_msgq_id, NULL };

        execve("./bin/client", args, NULL);

        panic("ERROR: Execve failed for client\n");
    }

    // NOTE: REMOVE THIS LATER
    wait(NULL);
}

void
init_worker(
    simctx_t *ctx,
    char     *shm_id, 
    size_t    idx
) {
    const pid_t pid = zfork();

    if (pid == 0) {
        ctx->roles[idx].worker = getpid();

        char *args[] = { "worker", shm_id, NULL };

        execve("./bin/worker", args, NULL);

        panic("ERROR: Execve failed for worker n. %zu\n", idx);
    }

    // NOTE: REMOVE THIS LATER
    wait(NULL);
}

int
_compare_pair_station(
    let_any a,
    let_any b
) {
    const struct pair_station* first  = (struct pair_station*)a;
    const struct pair_station* second = (struct pair_station*)b;

    return (second->avg_time - first->avg_time);
}

void
assign_roles(simctx_t* ctx) {
    location_t roles_buffer[NOF_WORKERS];
    int assigned_count = 0;

    roles_buffer[assigned_count++] = FIRST_COURSE; // 0
    roles_buffer[assigned_count++] = MAIN_COURSE;  // 1
    roles_buffer[assigned_count++] = COFFEE_BAR;   // 2
    roles_buffer[assigned_count++] = CHECKOUT;     // 3

    struct pair_station priority_list[4] = {
        { FIRST_COURSE, AVG_SRVC_FIRST_COURSE },
        { MAIN_COURSE,  AVG_SRVC_MAIN_COURSE  },
        { COFFEE_BAR,   AVG_SRVC_COFFEE      },
        { CHECKOUT,     AVG_SRVC_CASSA       }
    };

    qsort(priority_list, 4, sizeof(struct pair_station), _compare_pair_station);

    int p_index = 0;
    while (assigned_count < NOF_WORKERS) {
        roles_buffer[assigned_count++] = priority_list[p_index].id;

        p_index++;
        if (p_index > 3) p_index = 0;
    }

    for (int i = NOF_WORKERS - 1; i > 0; i--) {
        const int j = rand() % (i + 1);
        const location_t temp = roles_buffer[i];
        roles_buffer[i] = roles_buffer[j];
        roles_buffer[j] = temp;
    }

    for (int i = 0; i < NOF_WORKERS; i++) {
        ctx->roles[i].role = roles_buffer[i];
    }
}

void
sim_day(simctx_t* ctx) {
    assign_roles(ctx);
}
