#ifndef OBJECTS_H
#define OBJECTS_H

#include <stdbool.h>

#include "const.h"
#include <stddef.h>
#include <stdint.h>
#include <sys/_types/_pid_t.h>

#define DISH_NAME_MAX_LEN 32

// incorpora sia i tipi di stazioni sia dove puo' trovarsi un utente.
// nel caso delle stazioni 'TABLE' e' ignorato
typedef enum: uint8_t {
    FIRST_COURSE = 0,
    MAIN_COURSE  = 1,
    COFFEE_BAR       = 2,
    CHECKOUT     = 3,
    TABLE        = 4,
    EXIT         = 5
} location_t;

typedef struct {
    pid_t  pid;
    bool   active;
    size_t wait_time;   // time to wait for each pause
    size_t pause_time;  // cumulative time spent on pause
} worker;

typedef struct {
    pid_t      pid;
    bool       has_ticket;
    location_t current_location;

    size_t     taken_plates[4];
    int        plates_count;

    bool       served;
} client;

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
    MAIN,
    FIRST,
    SIDE,
    COFFEE
} dish_type;

typedef struct {
    location_t  type;
    struct {
        size_t  worked_time;
        size_t  wasted_time;
        size_t  served_dishes;
        size_t  left_dishes;
        size_t  earnings; // 0 for all non checkout stations
    } stats; // struct for cleaner code
    struct {
        int client_ids[10]; 
        int head;
        int tail;
        int count;
        int mutex_sem_id; 
    } client_queue;
    worker workers[NOF_WORKERS];
    dish_t menu   [MAX_DISHES];
} station;

typedef struct {
    station stations[4];
    client  clients [NOF_USERS];
} sim_ctx;

typedef struct {
    dish_available_t main_courses[MAX_MAIN_COURSES];
    dish_available_t first_courses[MAX_FIRST_COURSES];
    dish_available_t side_dishes[MAX_SIDE_DISHES];
    dish_available_t coffee_dishes[MAX_COFFEE_DISHES];

    dish_t main_courses_menu[MAX_MAIN_COURSES];
    dish_t first_courses_menu[MAX_FIRST_COURSES];
    dish_t side_dish_menu[MAX_SIDE_DISHES];
    dish_t coffee_menu[MAX_COFFEE_DISHES];

    size_t main_menu_size;
    size_t first_menu_size;
    size_t side_menu_size;
    size_t coffee_menu_size;

    bool is_sim_running;

} SharedData;



#endif
