#ifndef _OBJECTS_H
#define _OBJECTS_H

#include "const.h"
#include <stddef.h>
#include <stdbool.h>
#include <sys/_types/_ssize_t.h>
#include <sys/types.h>

#define DISH_NAME_MAX_LEN 32

union _semun {
    int val;
    struct semid_ds *buf;
    unsigned short *array;
};

typedef int    sem_t;
typedef size_t shmid_t;

// incorpora sia i tipi di stazioni sia dove puo' trovarsi un utente.
// nel caso delle stazioni 'TABLE' e' ignorato
typedef enum {
    FIRST_COURSE = 0,
    MAIN_COURSE  = 1,
    COFFEE_BAR   = 2,
    CHECKOUT     = 3,
    TABLE        = 4,
    EXIT         = 5
} loc_t;

struct pair_station{
    loc_t id;
    int   avg_time;
};

typedef struct {
    pid_t  pid;

    loc_t  role;
    size_t queue;

    size_t pause_time;  // cumulative time spent on pause
} worker_t;


typedef struct {
    pid_t  pid;
    bool   ticket;
    loc_t  loc;
    bool   served;
    size_t msgq;
    size_t wait_time;
    ssize_t dishes[MAX_DISHES];
} client_t;

typedef struct {
    size_t id;
    char   name[DISH_NAME_MAX_LEN];
    size_t price;
    size_t eating_time;
} dish_t;

typedef struct {
    size_t id;
    size_t quantity;
} dish_avl_t;

typedef enum {
    FIRST  = 0,
    MAIN   = 1,
    COFFEE = 2
} dish_type;

typedef struct {
    size_t worked_time;
    size_t wasted_time;
    size_t served_dishes;
    size_t left_dishes;
    size_t earnings; // 0 for all non checkout stations
} stats;

typedef struct {
    pid_t worker;
    loc_t role;
} worker_role_t;

typedef struct {
    // usato per aggiungere i worker e per le statistiche
    sem_t sem;
    stats stats;
    loc_t type;

    struct {    
        shmid_t shmid;
        size_t  cap;
        // a run time inizializzato a config.nof_wk_seats[type]
        // per gestire il massimo di lavoratori attivi (le pause)
        sem_t   sem;
    } wk_data;
    
    dish_t menu[DISHES_COUNT];
} station;

typedef struct {
    // Simulazione
    int sim_duration;
    int n_nano_secs;
    int overload_threshold;

    // Popolazione
    int nof_workers;
    int nof_users;
    int max_users_per_group;
    int nof_pause;

    // Tempi di servizio (AVG)
    int avg_srvc[NOF_STATIONS];
    // primi;
    // main_course;
    // coffee;
    // cassa;

    // Capacità (Posti)
    int nof_wk_seats[NOF_STATIONS];
    // primi;
    // main;
    // coffee;
    // cassa;

    int nof_tbl_seats;

    // Logistica Cibo & Versione Completa
    int avg_refill[2];
    // primi;
    // secondi;
    int max_porzioni[2];
    // primi;
    // secondi;
    int avg_refill_time;
    int stop_duration;
    int n_new_users;

} conf_t;

typedef struct {
    stats  global_stats;
    conf_t config;

    struct semaphores {
        sem_t shm;
        sem_t wk_end;
        sem_t out;
        sem_t tbl;
        sem_t wall;
    } sem;

    // Read && Write
    struct available_dishes {
        dish_avl_t data[MAX_ELEMENTS];
        size_t     size;
    } avl_dishes[3];

    // ONLY READ
    struct {
        dish_t data[MAX_ELEMENTS];
        size_t size;
    } menu[3];

    size_t id_msg_q[NOF_STATIONS + 1];

    bool is_sim_running;
    bool is_day_running;

} simctx_t;

#endif
