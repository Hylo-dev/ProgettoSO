#ifndef _OBJECTS_H
#define _OBJECTS_H

#include "const.h"
#include <stddef.h>
#include <stdbool.h>
#include <sys/types.h>

#define DISH_NAME_MAX_LEN 32

union _semun {
    int val;
    struct semid_ds *buf;
    unsigned short *array;
};

// incorpora sia i tipi di stazioni sia dove puo' trovarsi un utente.
// nel caso delle stazioni 'TABLE' e' ignorato
typedef enum {
    FIRST_COURSE = 0,
    MAIN_COURSE  = 1,
    COFFEE_BAR   = 2,
    CHECKOUT     = 3,
    TABLE        = 4,
    EXIT         = 5
} location_t;

struct pair_station{
    location_t id;
    int avg_time;
};

typedef struct {
    pid_t      pid;
    bool       active;

    location_t role;
    size_t     queue;

    size_t     pause_time;  // cumulative time spent on pause
} worker_t;

struct client_menu {
    size_t  cnt;
    ssize_t data[MAX_DISHES];
};

typedef struct {
    pid_t      pid;
    bool       ticket;
    location_t loc;
    bool       served;
    size_t     msgq;
    size_t     wait_time;
    struct client_menu dishes;
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
} dish_available_t;

typedef enum {
    FIRST  = 0,
    MAIN   = 1,
    COFFEE = 2
} dish_type;

typedef struct {
    size_t  worked_time;
    size_t  wasted_time;
    size_t  served_dishes;
    size_t  left_dishes;
    size_t  earnings; // 0 for all non checkout stations
} stats;

typedef struct {
    pid_t      worker;
    location_t role;
} worker_role_t;

typedef struct {
    int        sem;
    stats      stats;
    location_t type;

    struct {    
        int shmid;
        int cnt;
    } wk_data;
    
    dish_t     menu[DISHES_COUNT];
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
    int avg_srvc[4];
    // primi;
    // main_course;
    // coffee;
    // cassa;

    // Capacità (Posti)
    int nof_wk_seats[5];
    // primi;
    // main;
    // coffee;
    // cassa;
    // seats;

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

    // Read && Write
    struct available_dishes {
        dish_available_t elements[MAX_ELEMENTS];
        size_t size;
    } available_dishes[3];

    // ONLY READ
    struct {
        dish_t elements[MAX_ELEMENTS];
        size_t size;
    } menu[3];

    size_t id_msg_q[NOF_STATIONS + 1];

    int            shmid_roles;
    worker_role_t  *roles;

    bool is_sim_running;

} simctx_t;

#endif
