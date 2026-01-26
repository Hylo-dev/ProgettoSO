#ifndef _TOOLS_H
#define _TOOLS_H

#include <errno.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <stddef.h>
#include <sys/ipc.h>
#include <time.h>
#include <unistd.h>
#include <sys/msg.h>
#include <sys/shm.h>
#include <sys/types.h>
#include <sys/sem.h>

#include "const.h"
#include "objects.h"

#define SHM_RW 0666

#define DEBUG 0

// any type
typedef       void* any; 

// const any type
typedef const void* let_any; 

#define it(var, start, end) \
    for ( \
        int (var) = (int)(start); \
        (var) != (int)(end); \
        (var) += ((int)(start) < (int)(end) ? 1 : -1)\
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


/* ===================== ALLOC WRAPPER ===================== */

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
zshmget(size_t size){
    int res = shmget(IPC_PRIVATE, size, IPC_CREAT | SHM_RW);
    if (res < 0)
        panic("ERROR: Shared memory allocation is failed\n");
    return (size_t)res;
}

static inline any
zshmat(
    size_t shmid
){
    // Won't choose the address, the kern will do it (NULL)
    // default read and write flags (0)
    any res = shmat((int)shmid, NULL, 0);
    if (res == (void*)-1)
        panic("ERROR: Shared memory `at` is failed\n");
    return res;
}

static inline int
shm_kill(shmid_t id) {
    return shmctl((int)id, IPC_RMID, NULL);
}

static inline simctx_t*
get_ctx(size_t shmid) {
    return (simctx_t*)zshmat(shmid);
}

static inline station*
get_stations(size_t shmid) {
    return (station*)zshmat(shmid);
}

static inline worker_t*
get_workers(size_t shmid) {
    return (worker_t*)zshmat(shmid);
}



/* ====================== MSG WRAPPER ====================== */
static inline size_t
zmsgget(
    const key_t key,
    const int   mode
) {
    const int result = msgget(key, mode);
    if (result < 0)
        panic("ERROR: Creation message queue is failed\n");

    return (size_t)result;
}

static inline int
msg_kill(int id) {
    return msgctl(id, IPC_RMID, NULL);
}


/* ====================== SEM WRAPPER ====================== */

static inline sem_t
sem_init(const int val) {
    int sem_id = semget(IPC_PRIVATE, 1, IPC_CREAT | 0666);
    
    if (sem_id == -1)
        panic("ERROR: Semaphore init failed\n");

    union _semun arg;
    arg.val = val; 

    if (semctl(sem_id, 0, SETVAL, arg) == -1) 
        panic("ERROR: Semaphore init failed\n");
    
    return sem_id;
}

static inline int
sem_wait(const sem_t sem_id) {
    struct sembuf sb;
    sb.sem_num =  0;
    sb.sem_op  = -1;
    sb.sem_flg =  0;

    if (semop(sem_id, &sb, 1) == -1) {
        if (errno == EINTR || errno == EIDRM || errno == EINVAL) {
            return -1;
        }
        panic("ERROR: sem_wait failed, id %d, errno %d\n", sem_id, errno);
    }
    return 0;
}


// Operaxzione V: Incrementa (Signal)
static inline int
sem_signal(const sem_t sem_id) {
    struct sembuf sb;
    sb.sem_num = 0;
    sb.sem_op  = 1;
    sb.sem_flg = 0;

    if (semop(sem_id, &sb, 1) == -1) {
        if (errno == EINTR || errno == EIDRM || errno == EINVAL) {
            return -1;
        }
        panic("ERROR: sem_signal failed, id %d\n", sem_id);
    }

    return 0;
}

// Operazione: Setta un valore al semaforo
static inline void
sem_set(
    const sem_t id,
    const int   val
) {
    union _semun arg;
    arg.val = val;
    if (semctl(id, 0, SETVAL, arg) == -1)
        panic("ERROR: set_sem failed\n");
}

// Operazione: Aspetta che il sem sia = 0
static inline void
sem_wait_zero(const sem_t id) {
    struct sembuf sem_b;
    sem_b.sem_num = 0;
    sem_b.sem_op  = 0; // Usato per indicare di settarlo a zero
    sem_b.sem_flg = 0;

    if (semop(id, &sem_b, 1) == -1)
        panic("ERROR: sem_wait_zero failed\n");
}

static inline int
sem_getval(const sem_t id) {
    const int val = semctl(id, 0, GETVAL);
    if (val == -1)
        panic("ERROR: semctl GETVAL failed\n");

    return val;
}

static inline int
sem_kill(sem_t sem) {
    return semctl(sem, 0, IPC_RMID);
}


/* ===================== FILES ===================== */

static inline FILE*
zfopen(const char* fname, const char* mode) {
    FILE* file = fopen(fname, mode);
    if (file == NULL)
        panic("ERROR: Failed opening the file \"%s\" using the mode \"%s\"", fname, mode);

    return file;
}

static inline void
zfscanf(
          FILE *file,
    const char *fmt,
                ...
) {
    va_list args;
    va_start(args, fmt);
    
    if (vfscanf(file, fmt, args) < 1)
        panic("ERROR: Failed read values\n");
}

static inline long
zfsize(FILE* file) {
    long fsize;
    fseek(file, 0, SEEK_END);
    fsize = ftell(file);
    fseek(file, 0, SEEK_SET);
    return fsize;
}

/* ======================= SUPPORT  ========================*/


static void
zprintf(
    const sem_t sem_id,
    const char *fmt, ...
) {
    va_list args;

    sem_wait(sem_id);

    va_start(args, fmt);
    if (DEBUG) {
        vprintf(fmt, args);
        fflush(stdout);
    } else {
        FILE *f = fopen("data/simulation.log", "a");
        vfprintf(f, fmt, args);
        fclose(f);
    }
    va_end(args);

    sem_signal(sem_id);
}

static inline void
znsleep(const size_t wait_time) {
    struct timespec req;

    const size_t total_ns = wait_time * N_NANO_SECS;

    req.tv_sec  = (time_t)(total_ns / TO_NANOSEC);
    req.tv_nsec = (long)  (total_ns % TO_NANOSEC);

    nanosleep(&req, NULL);
}

static inline size_t
atos(const char* str) {
    return (size_t)atoi(str);
}

static inline bool
atob(const char* str) {
    return (bool)atoi(str);
}

static inline char*
itos(const int val) {
    static char buffers[8][12]; 
    static int idx = 0;

    char* current = buffers[idx];
    
    idx = (idx + 1) % (sizeof(buffers) / sizeof(buffers[0]));

    snprintf(current, sizeof(buffers[0]), "%d", val);
    
    return current;
}

static inline size_t
get_service_time(
    size_t avg_time,
    size_t percent
) {
    if (avg_time == 0) return 0;

    const size_t delta     = (avg_time * percent) / 100;
    const long   variation = (long)(rand() % (2 * delta + 1) - delta);
    const long   result    = (long)avg_time - variation;
    
    return (result > 0) ? (size_t)result : 0;
}



static inline void
handle_signal(int sig) {
    (void)sig;
}


#endif
