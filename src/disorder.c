//
// Created by Eliomar Alejandro Rodriguez Ferrer on 26/01/26.
//

#include "tools.h"
#include <stdbool.h>
#include <stdio.h>
#include <unistd.h>
#include <signal.h>

static sem_t g_sem_disorder = -1;
static simctx_t *g_ctx = NULL;

static inline void
cleanup_and_exit(int sig) {
    if (g_ctx && g_sem_disorder != -1) {

        if (g_ctx->is_disorder_active) {
            g_ctx->is_disorder_active = false;
			sem_signal(g_sem_disorder);

            const char msg[] = "\n[DISORDER] Interrotto! Ripristino stato.\n";
            write(STDERR_FILENO, msg, sizeof(msg) - 1);
        }
    }

    exit(0);
}

static inline void
read_shm_id(int *ctx_id, int *shm_st) {
    FILE *file = zfopen("data/shared", "r");

    zfscanf(file, "%d, %d", ctx_id, shm_st);

    if (*ctx_id == -1 || *shm_st == -1) {
        fclose(file);
        panic("ERROR: Context not exist!\n");
    }

    // printf("Test, read value: %d, %d\n", *ctx_id, *shm_st);
    fclose(file);
}

int
main(void) {
    int ctx_id = -1, shm_st = -1;

	struct sigaction sa;
    sa.sa_handler = cleanup_and_exit;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    read_shm_id(&ctx_id, &shm_st);
    simctx_t *ctx = get_ctx(ctx_id);

	g_ctx = ctx;
    g_sem_disorder = ctx->sem[disorder];

    if (ctx->is_disorder_active) {
        printf("[DISORDER] Errore: Disordine gia' in corso!\n");
        return 1;
    }

    printf("[INFO] Disorder started\n\n");
	ctx->is_disorder_active = true;
	sem_wait(g_sem_disorder);

	size_t value = sem_getval(g_sem_disorder);
	printf("[INFO] Set signal Value %zu\n", value);

    znsleep(ctx->config.disorder_duration);

    ctx->is_disorder_active = false;
    sem_signal(g_sem_disorder);

	value = sem_getval(g_sem_disorder);
	printf("[INFO] Set wait %zu\n", value);

    printf("\n[INFO] End disorder executable.\n");
    return 0;
}
