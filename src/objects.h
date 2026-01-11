#ifndef _OBJECTS_H
#define _OBJECTS_H

#include <stdbool.h>

#include "const.h"
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#define DISH_NAME_MAX_LEN 32

// incorpora sia i tipi di stazioni sia dove puo' trovarsi un utente.
// nel caso delle stazioni 'TABLE' e' ignorato
typedef enum: uint8_t {
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
    pid_t  pid;
    bool   active;
    size_t wait_time;   // time to wait for each pause
    size_t pause_time;  // cumulative time spent on pause
} worker;



struct client_menu {
    size_t cnt;
    size_t data[MAX_DISHES];
};

typedef struct {
    pid_t      pid;
    bool       ticket;
    location_t loc;
    bool       served;
    size_t     msgq;
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
    stats      stats;
    location_t type;
    worker     workers[NOF_WORKERS];
    dish_t     menu   [DISHES_COUNT];
} station;

typedef struct {
    stats global_stats;

    dish_available_t first_courses[MAX_FIRST_COURSES];
    dish_available_t main_courses[MAX_MAIN_COURSES];
    dish_available_t coffee_dishes[MAX_COFFEE_DISHES];

    dish_t main_courses_menu[MAX_MAIN_COURSES];
    dish_t first_courses_menu[MAX_FIRST_COURSES];
    dish_t coffee_menu[MAX_COFFEE_DISHES];

    // Message queues
    size_t id_msg_q[NOF_STATIONS];

    struct {
        pid_t      worker;
        location_t role;
    } roles[NOF_WORKERS];

    size_t main_menu_size;
    size_t first_menu_size;
    size_t coffee_menu_size;

    bool is_sim_running;

} simctx_t;



#endif
