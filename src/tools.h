#ifndef _TOOLS_H
#define _TOOLS_H

#include <errno.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <stddef.h>
#include <time.h>
#include <unistd.h>
#include <sys/msg.h>
#include <sys/shm.h>
#include <sys/types.h>
#include <sys/sem.h>

#include "const.h"

// any type
typedef       void* any; 

// const any type
typedef const void* let_any; 

#define it(var, start, end) \
    for ( \
        int var = (int)(start); \
        var != (int)(end); \
        var += ((int)(start) < (int)(end) ? 1 : -1)\
    )

static inline void
panic(const char* fmt, ...) {
    int err = errno;

    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);

    exit(err ? err : EXIT_FAILURE);
}

static inline any
zmalloc(size_t size) {
    any ptr = malloc(size);
    if (!ptr)   panic("ERROR: Malloc failed\n");
    return ptr;
}

static inline any
zrealloc(
    any    old_ptr,
    size_t size
) {
    any ptr = realloc(old_ptr, size);
    if (!ptr) panic("ERROR: Calloc failed\n");
    return ptr;
}

static inline any
zcalloc(
    size_t new_size,
    size_t size
) {
    any ptr = calloc(new_size, size);
    if (!ptr) panic("ERROR: Calloc failed\n");
    return ptr;
}

/* ====================== UNI WRAPPER ====================== */
static inline pid_t
zfork() {
    pid_t pid = fork();
    if (pid < 0)
        panic("ERROR: Failed to launch a new process (fork)\n");
    return pid;
}

/* ====================== SHM WRAPPER ====================== */
static inline size_t
zshmget(key_t key, size_t size, int mode){
    int res = shmget(key, size, mode);
    if (res < 0)
        panic("ERROR: Shared memory allocation is failed\n");
    return (size_t)res;
}

static inline any
zshmat(
          size_t shmid,
    const any    shmaddr,
          int    shmflg
){
    any res = shmat((int)shmid, shmaddr, shmflg);
    if (res == (void*)-1)
        panic("ERROR: Shared memory `at` is failed\n");
    return res;
}

/* ====================== MSG WRAPPER ====================== */
static inline size_t
zmsgget(
    const key_t key,
    const int   mode
) {
    int result = msgget(key, mode);
    if (result < 0)
        panic("ERROR: Creation message queue is failed\n");

    return (size_t)result;
}


static void
sem_wait(
    const int sem_id,
    const int sem_num
) {
    struct sembuf sops;
    sops.sem_num = (unsigned short)sem_num;
    sops.sem_op  = -1;
    sops.sem_flg = 0;

    // Manage INTR SIG (EINTR)
    while (semop(sem_id, &sops, 1) == -1) {
        if (errno != EINTR) {
            perror("ERROR: sem_wait interrupt");
            break; 
        }
    }
}

static void
sem_signal(
    const int sem_id,
    const int sem_num
) {
    struct sembuf sops;
    sops.sem_num = (unsigned short)sem_num;
    sops.sem_op  = 1;
    sops.sem_flg = 0;

    if (semop(sem_id, &sops, 1) == -1) {
        perror("ERROR: sem_signal failed");
    }
}

static void
zprintf(
    const int   sem_id,
    const char *fmt, ...
) {
    va_list args;

    sem_wait(sem_id, 0);

    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);

    fflush(stdout);

    sem_signal(sem_id, 0);
}

static inline void
znsleep(const size_t wait_time) {
    struct timespec req;

    const size_t total_ns = wait_time * N_NANO_SECS;

    req.tv_sec = (time_t)(total_ns / 1000000000L);
    req.tv_nsec = (long)(total_ns % 1000000000L);

    nanosleep(&req, NULL);
}


/* ===================== FILES ===================== */

static inline FILE*
zfopen(const char* fname, const char* mode) {
    FILE* file = fopen(fname, mode);
    if (file == NULL)
        panic("ERROR: Failed opening the file \"%s\" using the mode \"%s\"", fname, mode);

    return file;
}

static inline long
zfsize(FILE* file) {
    long fsize;
    fseek(file, 0, SEEK_END);
    fsize = ftell(file);
    fseek(file, 0, SEEK_SET);
    return fsize;
}

static inline size_t
atos(const char* str) {
    return (size_t)atoi(str);
}

#endif
