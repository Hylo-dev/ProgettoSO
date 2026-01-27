#include "objects.h"
#include "tools.h"
#include <signal.h>
#include <stdio.h>

int
load_shmids(int* ctx_id, int* st_id) {
    FILE *file = zfopen("data/shared", "r");
    zfscanf(file, "%d, %d", ctx_id, st_id);
    fclose(file);

    if (*ctx_id == -1 || *st_id == -1)
        return -1;

    return 0;
}

pid_t
get_mainpid() {
    pid_t pid;
    FILE *f_pid = zfopen("data/main.pid", "r");
    zfscanf(f_pid, "%d", &pid);
    fclose(f_pid);

    if (pid == -1)
        panic("ERROR: Main process not found\n");

    return pid;
}

int
main(int argc, char *argv[]) {
    if (argc != 2)
        panic("Usage: %s <num_users>\n", argv[0]);

    int users_to_add = atoi(argv[1]);
    if (users_to_add <= 0) return 0;

    int ctx_id, st_id;
    if (load_shmids(&ctx_id, &st_id) == -1)
        panic("ERROR: Unable to load shmid\n");

    pid_t main_pid = get_mainpid();
    
    simctx_t *ctx = get_ctx((size_t)ctx_id);

    sem_wait(ctx->sem[shm]);
    ctx->added_users = (size_t)users_to_add;
    sem_signal(ctx->sem[shm]);

    // 4. Invia il segnale
    printf("Invio segnale al main (PID: %d) per aggiungere %d utenti...\n", main_pid, users_to_add);
    if (kill(main_pid, SIGUSR2) == -1) 
        panic("Errore nell'invio del segnale\n");

    return 0;
}
