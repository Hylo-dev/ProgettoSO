#ifndef _CONFIG_H
#define _CONFIG_H

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#include "cJSON.h"
#include "objects.h"
#include "tools.h"

#define PARSE_INT(json, key, field) \
    do { \
        cJSON *item = cJSON_GetObjectItemCaseSensitive(json, key); \
        if (cJSON_IsNumber(item)) { \
            conf->field = item->valueint; \
        } else \
            panic("ERROR: Unknown config format, around key: %s\n", key); \
    } while(0)
    

static void
load_config(
    const char   *filename,
          conf_t *conf
) {
   
    FILE  *file         = NULL;
    char  *json_content = NULL;
    long   file_size    = 0;
    cJSON *json         = NULL;

    // 1. Apertura File
    file = zfopen(filename, "rb");
    file_size = zfsize(file);
    
    json_content = (char *)malloc(file_size + 1);
    if (!json_content) {
        fclose(file);
        panic("ERROR: Malloc failed allocating %d bytes", file_size+1);
    }

    // 3. Lettura contenuto
    fread(json_content, 1, file_size, file);
    json_content[file_size] = '\0'; // Terminatore stringa
    fclose(file);

    // 4. Parsing JSON
    json = cJSON_Parse(json_content);
    if (json == NULL) {
        const char *error_ptr = cJSON_GetErrorPtr();
        if (error_ptr != NULL) {
            panic("ERROR: Syntax error near: %s\n", error_ptr);
        }
        free(json_content);
        panic("ERROR: Invalid JSON");
    }

    PARSE_INT(json, "SIM_DURATION",         sim_duration);
    assert(conf->sim_duration > 0 && "SIM_DURATION must be greater than 0");

    PARSE_INT(json, "N_NANO_SECS",          n_nano_secs);
    assert(conf->n_nano_secs > 0 && "N_NANO_SECS must be greater than 0");

    PARSE_INT(json, "OVERLOAD_THRESHOLD",   overload_threshold);
    assert(conf->overload_threshold > 0 && "OVERLOAD_THRESHOLD must be greater than 0");

    PARSE_INT(json, "NOF_USERS",            nof_users);
    assert(conf->nof_users > 0 && "NOF_USERS must be greater than 0");

    PARSE_INT(json, "MAX_USERS_PER_GROUP",  max_users_per_group);
    assert(conf->max_users_per_group >= 1 && "MAX_USERS_PER_GROUP must be >= 1");

    PARSE_INT(json, "NOF_PAUSE",            nof_pause);
    assert(conf->nof_pause >= 0 && "NOF_PAUSE must be >= 0");

    PARSE_INT(json, "AVG_SRVC_PRIMI",       avg_srvc[FIRST_COURSE]);
    assert(conf->avg_srvc[FIRST_COURSE] > 0 && "AVG_SRVC_PRIMI must be > 0");

    PARSE_INT(json, "AVG_SRVC_MAIN_COURSE", avg_srvc[MAIN_COURSE]);
    assert(conf->avg_srvc[MAIN_COURSE] > 0 && "AVG_SRVC_MAIN_COURSE must be > 0");

    PARSE_INT(json, "AVG_SRVC_COFFEE",      avg_srvc[COFFEE_BAR]);
    assert(conf->avg_srvc[COFFEE_BAR] > 0 && "AVG_SRVC_COFFEE must be > 0");

    PARSE_INT(json, "AVG_SRVC_CASSA",       avg_srvc[CHECKOUT]);
    assert(conf->avg_srvc[CHECKOUT] > 0 && "AVG_SRVC_CASSA must be > 0");

    PARSE_INT(json, "NOF_WK_SEATS_PRIMI",   nof_wk_seats[FIRST_COURSE]);
    assert(conf->nof_wk_seats[FIRST_COURSE] > 0 && "NOF_WK_SEATS_PRIMI must be > 0");

    PARSE_INT(json, "NOF_WK_SEATS_SECONDI", nof_wk_seats[MAIN_COURSE]);
    assert(conf->nof_wk_seats[MAIN_COURSE] > 0 && "NOF_WK_SEATS_SECONDI must be > 0");

    PARSE_INT(json, "NOF_WK_SEATS_COFFEE",  nof_wk_seats[COFFEE_BAR]);
    assert(conf->nof_wk_seats[COFFEE_BAR] > 0 && "NOF_WK_SEATS_COFFEE must be > 0");

    PARSE_INT(json, "NOF_WK_SEATS_CASSA",   nof_wk_seats[CHECKOUT]);
    assert(conf->nof_wk_seats[CHECKOUT] > 0 && "NOF_WK_SEATS_CASSA must be > 0");

    PARSE_INT(json, "NOF_TABLE_SEATS",      nof_wk_seats[TABLE]);
    assert(conf->nof_wk_seats[TABLE] > 0 && "NOF_TABLE_SEATS must be > 0");

    PARSE_INT(json, "AVG_REFILL_PRIMI",     avg_refill[FIRST_COURSE]);
    assert(conf->avg_refill[FIRST_COURSE] > 0 && "AVG_REFILL_PRIMI must be > 0");

    PARSE_INT(json, "AVG_REFILL_SECONDI",   avg_refill[MAIN_COURSE]);
    assert(conf->avg_refill[MAIN_COURSE] > 0 && "AVG_REFILL_SECONDI must be > 0");

    PARSE_INT(json, "MAX_PORZIONI_PRIMI",   max_porzioni[FIRST_COURSE]);
    assert(conf->max_porzioni[FIRST_COURSE] >= conf->avg_refill[FIRST_COURSE]
           && "MAX_PORZIONI_PRIMI must be >= AVG_REFILL_PRIMI");

    PARSE_INT(json, "MAX_PORZIONI_SECONDI", max_porzioni[MAIN_COURSE]);
    assert(conf->max_porzioni[MAIN_COURSE] >= conf->avg_refill[MAIN_COURSE]
           && "MAX_PORZIONI_SECONDI must be >= AVG_REFILL_SECONDI");

    PARSE_INT(json, "AVG_REFILL_TIME",      avg_refill_time);
    assert(conf->avg_refill_time > 0 && "AVG_REFILL_TIME must be > 0");

    PARSE_INT(json, "STOP_DURATION",        stop_duration);
    assert(conf->stop_duration >= 0 && "STOP_DURATION must be >= 0");

    PARSE_INT(json, "N_NEW_USERS",          n_new_users);
    assert(conf->n_new_users >= 0 && "N_NEW_USERS must be >= 0");

    cJSON_Delete(json);
    free(json_content);
}

#endif
