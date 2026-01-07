#ifndef OBJECTS_H
#define OBJECTS_H

#include "const.h"
#include "../libds/queue.h"
#include "../libds/dict.h"
#include <stddef.h>
#include <sys/_types/_pid_t.h>


// incorpora sia i tipi di stazioni sia dove puo' trovarsi un utente.
// nel caso delle stazioni 'TABLE' e' ignorato
typedef enum: uint8_t {
    FIRST_COURSE,
    MAIN_COURSE,
    COFFEE,
    CHECKOUT,
    TABLE
} location;

// not final
typedef struct {
    pid_t  pid;
    bool   active;
    size_t wait_time;   // time to wait for each pause
    size_t pause_time;  // cumulative time spent on pause
} worker;

// not final
typedef struct {
    pid_t    pid;
    bool     has_ticket;
    location current_location;
    size_t   taken_plates[4];
    bool     served;
} client;

typedef struct {
    size_t price;
    size_t eating_time;
} menu_entry;

typedef struct {
    location  type;
    queue     serving;
    worker*   workers;
    struct {
        size_t worked_time;
        size_t wasted_time;
        size_t served_dishes;
        size_t left_dishes;
        size_t earnings; // 0 for all non checkout stations
    } stats; // struct for cleaner code

    // TODO: mettere nella memoria contigua che venga condiviso anche la parte di memeria del dict
    size_t dict_offset;
} station;

typedef struct {
    station stations[4];
    client  clients [NOF_USERS];
} sim_ctx;



#endif
