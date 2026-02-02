#ifndef _TOOLS_H
#define _TOOLS_H

#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/msg.h>
#include <sys/sem.h>
#include <sys/shm.h>
#include <sys/types.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#include "const.h"
#include "objects.h"

/* =========================================================================
 * Macros and Types
 * ========================================================================= */

#define SHM_RW 0666
#define DEBUG 0

/**
 * Generic pointer type for better readability.
 */
typedef void *any;

/**
 * Constant generic pointer type.
 */
typedef const void *let_any;

/**
 * Iterator macro for range-based loops.
 * Automatically determines direction based on start/end values.
 */
#define it(var, start, end)                                                    \
    for (int(var) = (int)(start); (var) != (int)(end);                         \
         (var) += ((int)(start) < (int)(end) ? 1 : -1))

/* =========================================================================
 * Function Prototypes
 * ========================================================================= */

/* Error Handling */
static inline void panic(const char *fmt, ...);

/* Memory Allocation Wrappers */
static inline any zmalloc(size_t size);
static inline any zrealloc(any old_ptr, size_t size);
static inline any zcalloc(size_t new_size, size_t size);

/* Process Management */
static inline pid_t zfork();

/* Semaphore IPC */
static inline sem_t sem_init(const int val);
static inline int   sem_wait(const sem_t sem_id);
static inline int   sem_signal(const sem_t sem_id);
static inline void  sem_set(const sem_t id, const int val);
static inline int   sem_wait_zero(const sem_t id);
static inline int   sem_getval(const sem_t id);
static inline int   sem_kill(sem_t sem);

/* Shared Memory IPC */
static inline size_t    zshmget(size_t size);
static inline any       zshmat(size_t shmid);
static inline int       shm_kill(shmid_t id);
static inline simctx_t *get_ctx(size_t shmid);
static inline station  *get_stations(size_t shmid);
static inline worker_t *get_workers(size_t shmid);

/* Message Queue IPC */
static inline size_t zmsgget(const key_t key, const int mode);
static inline int    msg_kill(int id);

/* File Operations */
static inline FILE *zfopen(const char *fname, const char *mode);
static inline void  zfscanf(FILE *file, const char *fmt, ...);
static inline long  zfsize(FILE *file);
static void         fclear(const char *filename);
static inline void
save_stats_csv(const simctx_t *ctx, const station *stations, const size_t day);

/* General Utilities */
static void          zprintf(const sem_t sem_id, const char *fmt, ...);
static inline void   znsleep(const size_t wait_time);
static inline size_t atos(const char *str);
static inline bool   atob(const char *str);
static inline char  *itos(const int val);
static inline size_t get_service_time(size_t avg_time, size_t percent);
static inline void   handle_signal(int sig);

/* =========================================================================
 * Implementation: Error Handling
 * ========================================================================= */

/**
 * Print an error message and terminate the process.
 * If errno is set, it exits with the error code, otherwise EXIT_FAILURE.
 */
static inline void
panic(const char *fmt, ...) {
    int err = errno;

    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);

    exit(err ? err : EXIT_FAILURE);
}

/* =========================================================================
 * Implementation: Memory Allocation Wrappers
 * ========================================================================= */

/**
 * Safe malloc wrapper that panics on failure.
 */
static inline any
zmalloc(size_t size) {
    any ptr = malloc(size);
    if (!ptr)
        panic("ERROR: Malloc failed\n");
    return ptr;
}

/**
 * Safe realloc wrapper that panics on failure.
 */
static inline any
zrealloc(any old_ptr, size_t size) {
    any ptr = realloc(old_ptr, size);
    if (!ptr)
        panic("ERROR: Realloc failed\n");
    return ptr;
}

/**
 * Safe calloc wrapper that panics on failure.
 */
static inline any
zcalloc(size_t new_size, size_t size) {
    any ptr = calloc(new_size, size);
    if (!ptr)
        panic("ERROR: Calloc failed\n");
    return ptr;
}

/* =========================================================================
 * Implementation: Process Management
 * ========================================================================= */

/**
 * Safe fork wrapper that panics on failure.
 */
static inline pid_t
zfork() {
    pid_t pid = fork();
    if (pid < 0)
        panic("ERROR: Failed to launch a new process (fork)\n");
    return pid;
}

/* =========================================================================
 * Implementation: Semaphore IPC
 * ========================================================================= */

/**
 * Initialize a private semaphore with a specific value.
 */
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

/**
 * Perform a P operation (Wait).
 * Returns -1 if interrupted or the semaphore is removed.
 */
static inline int
sem_wait(const sem_t sem_id) {
    struct sembuf sb;
    sb.sem_num = 0;
    sb.sem_op  = -1;
    sb.sem_flg = SEM_UNDO;

    if (semop(sem_id, &sb, 1) == -1) {
        if (errno == EINTR || errno == EIDRM || errno == EINVAL) {
            return -1;
        }
        panic("ERROR: sem_wait failed, id %d, errno %d\n", sem_id, errno);
    }
    return 0;
}

/**
 * Perform a V operation (Signal/Increment).
 */
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

/**
 * Explicitly set the value of a semaphore.
 */
static inline void
sem_set(const sem_t id, const int val) {
    union _semun arg;
    arg.val = val;

    if (semctl(id, 0, SETVAL, arg) == -1)
        panic("ERROR: set_sem failed\n");
}

/**
 * Block the process until the semaphore value reaches zero.
 */
static inline int
sem_wait_zero(const sem_t id) {
    struct sembuf sem_b;
    sem_b.sem_num = 0;
    sem_b.sem_op  = 0;
    sem_b.sem_flg = 0;

    if (semop(id, &sem_b, 1) == -1) {
        if (errno == EINTR || errno == EIDRM || errno == EINVAL) {
            return -1;
        }
        panic("ERROR: sem_wait_zero failed\n");
    }

    return 0;
}

/**
 * Get the current value of the semaphore.
 */
static inline int
sem_getval(const sem_t id) {
    const int val = semctl(id, 0, GETVAL);
    if (val == -1)
        panic("ERROR: semctl GETVAL failed\n");

    return val;
}

/**
 * Remove the semaphore from the system.
 */
static inline int
sem_kill(sem_t sem) {
    return semctl(sem, 0, IPC_RMID);
}

/* =========================================================================
 * Implementation: Shared Memory IPC
 * ========================================================================= */

/**
 * Allocate a private shared memory segment.
 */
static inline size_t
zshmget(size_t size) {
    int res = shmget(IPC_PRIVATE, size, IPC_CREAT | SHM_RW);
    if (res < 0)
        panic("ERROR: Shared memory allocation is failed\n");
    return (size_t)res;
}

/**
 * Attach the shared memory segment to the process address space.
 */
static inline any
zshmat(size_t shmid) {
    /* The kernel chooses the address (NULL), default R/W flags (0) */
    any res = shmat((int)shmid, NULL, 0);
    if (res == (void *)-1)
        panic("ERROR: Shared memory `at` is failed\n");
    return res;
}

/**
 * Mark shared memory for destruction.
 */
static inline int
shm_kill(shmid_t id) {
    return shmctl((int)id, IPC_RMID, NULL);
}

/**
 * Helper to attach and cast the simulation context.
 */
static inline simctx_t *
get_ctx(size_t shmid) {
    return (simctx_t *)zshmat(shmid);
}

/**
 * Helper to attach and cast the stations array.
 */
static inline station *
get_stations(size_t shmid) {
    return (station *)zshmat(shmid);
}

/**
 * Helper to attach and cast the workers array.
 */
static inline worker_t *
get_workers(size_t shmid) {
    return (worker_t *)zshmat(shmid);
}

/* =========================================================================
 * Implementation: Message Queue IPC
 * ========================================================================= */

/**
 * Safe message queue creation wrapper.
 */
static inline size_t
zmsgget(const key_t key, const int mode) {
    const int result = msgget(key, mode);
    if (result < 0)
        panic("ERROR: Creation message queue is failed\n");

    return (size_t)result;
}

/**
 * Remove a message queue.
 */
static inline int
msg_kill(int id) {
    return msgctl(id, IPC_RMID, NULL);
}

/* =========================================================================
 * Implementation: File Operations
 * ========================================================================= */

/**
 * Safe fopen wrapper.
 */
static inline FILE *
zfopen(const char *fname, const char *mode) {
    FILE *file = fopen(fname, mode);
    if (file == NULL)
        panic(
            "ERROR: Failed opening the file \"%s\" using the mode \"%s\"",
            fname, mode
        );

    return file;
}

/**
 * Safe vfscanf wrapper.
 */
static inline void
zfscanf(FILE *file, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);

    if (vfscanf(file, fmt, args) < 1)
        panic("ERROR: Failed read values\n");
}

/**
 * Calculate the size of a file in bytes.
 */
static inline long
zfsize(FILE *file) {
    long fsize;
    fseek(file, 0, SEEK_END);
    fsize = ftell(file);
    fseek(file, 0, SEEK_SET);
    return fsize;
}

/**
 * Truncate a file to zero length.
 */
static void
fclear(const char *filename) {
    FILE *fp = zfopen(filename, "w");
    fclose(fp);
}

/**
 * Appends simulation statistics to a CSV file.
 * Handles header generation if the file is new/empty.
 */
static inline void
save_stats_csv(const simctx_t *ctx, const station *stations, const size_t day) {
    FILE *file = fopen("data/stats.csv", "a");
    if (!file) {
        if (DEBUG)
            perror("Could not open CSV file");
        return;
    }

    const size_t users_served_day = (size_t)sem_getval(ctx->sem[cl_end]);
    const size_t users_unserved_day =
        (size_t)ctx->config.nof_users - users_served_day;

    const size_t srv_primi   = stations[FIRST_COURSE].stats.served_dishes;
    const size_t srv_secondi = stations[MAIN_COURSE].stats.served_dishes;
    const size_t srv_caffe   = stations[COFFEE_BAR].stats.served_dishes;
    const size_t srv_tot     = srv_primi + srv_secondi + srv_caffe;

    size_t left_primi = 0;
    it(k, 0, ctx->avl_dishes[FIRST_COURSE].size) {
        left_primi += ctx->avl_dishes[FIRST_COURSE].data[k].quantity;
    }

    size_t left_secondi = 0;
    it(k, 0, ctx->avl_dishes[MAIN_COURSE].size) {
        left_secondi += ctx->avl_dishes[MAIN_COURSE].data[k].quantity;
    }

    const size_t earn_day   = stations[CHECKOUT].stats.earnings;
    size_t       breaks_day = 0;
    it(i, 0, NOF_STATIONS) breaks_day += stations[i].stats.total_breaks;

    const double avg_breaks_day =
        ctx->config.nof_workers > 0
            ? (double)breaks_day / ctx->config.nof_workers
            : 0.0;

    const size_t glob_unserved = ctx->global_stats.users_not_served;
    const size_t glob_earn     = ctx->global_stats.earnings;

    const size_t total_potential_users =
        (size_t)ctx->config.nof_users * (day + 1);
    const size_t glob_served_users = total_potential_users - glob_unserved;

    const double avg_users_served = (double)glob_served_users / (day + 1);
    const double avg_earn         = (double)glob_earn / (day + 1);

    const long fsize = zfsize(file);
    if (fsize == 0) {
        fprintf(
            file,
            "Day,"
            "Users_Served_Day,Users_Unserved_Day,"
            "Users_Unserved_Total,Avg_Users_Served_Day,"
            "Total_Dishes,First_Course_Served,Main_Course_Served,Coffee_Served,"
            "Leftover_First,Leftover_Main,"
            "Earnings_Day,Total_Breaks_Day,Avg_Breaks_Day,"
            "Total_Earnings,Avg_Earnings_Day\n"
        );
    }

    fprintf(
        file,
        "%zu,%zu,%zu,%zu,%.2f,%zu,%zu,%zu,%zu,%zu,%zu,%zu,%zu,%.2f,%zu,%.2f\n",
        day + 1, users_served_day, users_unserved_day, glob_unserved,
        avg_users_served, srv_tot, srv_primi, srv_secondi, srv_caffe,
        left_primi, left_secondi, earn_day, breaks_day, avg_breaks_day,
        glob_earn, avg_earn
    );

    fclose(file);
}

/* =========================================================================
 * Implementation: General Utilities
 * ========================================================================= */

/**
 * Thread-safe (process-safe) print function using a semaphore.
 * If DEBUG is enabled, prints to stdout, otherwise logs to file.
 */
static void
zprintf(const sem_t sem_id, const char *fmt, ...) {
    va_list args;

    sem_wait(sem_id);

    va_start(args, fmt);
    if (DEBUG) {
        vprintf(fmt, args);
        fflush(stdout);
    } else {
        FILE *f = fopen("data/simulation.log", "a");
        if (f) {
            vfprintf(f, fmt, args);
            fclose(f);
        }
    }
    va_end(args);

    sem_signal(sem_id);
}

/**
 * High-resolution sleep based on simulation constants.
 */
static inline void
znsleep(const size_t wait_time) {
    struct timespec req;

    const size_t total_ns = wait_time * N_NANO_SECS;

    req.tv_sec  = (time_t)(total_ns / TO_NANOSEC);
    req.tv_nsec = (long)(total_ns % TO_NANOSEC);

    nanosleep(&req, NULL);
}

/**
 * String to size_t.
 */
static inline size_t
atos(const char *str) {
    return (size_t)atoi(str);
}

/**
 * String to boolean.
 */
static inline bool
atob(const char *str) {
    return (bool)atoi(str);
}

/**
 * Integer to string.
 * Uses a static circular buffer of strings to allow up to 8 calls
 * within the same printf-style statement safely.
 */
static inline char *
itos(const int val) {
    static char buffers[8][12];
    static int  idx = 0;

    char *current = buffers[idx];

    idx = (idx + 1) % (sizeof(buffers) / sizeof(buffers[0]));

    snprintf(current, sizeof(buffers[0]), "%d", val);

    return current;
}

/**
 * Calculates a service time with random variation.
 * @param avg_time The base average time.
 * @param percent The maximum percentage variation (delta).
 */
static inline size_t
get_service_time(size_t avg_time, size_t percent) {
    if (avg_time == 0)
        return 0;

    const size_t delta     = (avg_time * percent) / 100;
    const long   variation = (long)(rand() % (2 * delta + 1) - delta);
    const long   result    = (long)avg_time - variation;

    return (result > 0) ? (size_t)result : 0;
}

/**
 * Placeholder for signal handling logic.
 */
static inline void
handle_signal(int sig) {
    (void)sig;
}

#endif
