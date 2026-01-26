//
// Created by Eliomar Alejandro Rodriguez Ferrer on 26/01/26.
//

#include "tools.h"
#include <stdbool.h>
#include <stdio.h>
#include <unistd.h>

#define loop for (;;)

static inline void
read_shm_id(int *ctx_id, int *shm_st) {
    FILE *file = zfopen("data/shared", "r");

    zfscanf(file, "%d, %d", ctx_id, shm_st);

    if (*ctx_id == -1 || *shm_st == -1) {
        fclose(file);
        panic("ERROR: Context not exist!\n");
    }

    printf("Test, read value: %d, %d\n", *ctx_id, *shm_st);
    fclose(file);
}

int
main(void) {
    int ctx_id = -1, shm_st = -1;

    read_shm_id(&ctx_id, &shm_st);
    simctx_t *ctx = get_ctx(ctx_id);
    station  *st  = get_stations((size_t)shm_st);

    printf("[INFO] Disorder started\n");
    const int   seats    = ctx->config.nof_wk_seats[CHECKOUT];
    const sem_t checkout = st[CHECKOUT].sem;

    sem_wait(checkout);
    ctx->is_disorder_active = true;

    znsleep(ctx->config.disorder_duration);

    ctx->is_disorder_active = false;
    sem_signal(checkout);

    printf("[INFO] End disorder executable.\n");
    return 0;
}
