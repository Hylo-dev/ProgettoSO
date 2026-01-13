#ifndef _CONFIG_H
#define _CONFIG_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cJSON.h"
#include "tools.h"

#define PARSE_INT(json, key, field) \
    do { \
        cJSON *item = cJSON_GetObjectItemCaseSensitive(json, key); \
        if (cJSON_IsNumber(item)) { \
            conf->field = item->valueint; \
        } else \
            panic("ERROR: Unknown config format, around key: %s\n", key); \
    } while(0)
    
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
    int avg_srvc_primi;
    int avg_srvc_main_course;
    int avg_srvc_coffee;
    int avg_srvc_cassa;

    // Capacità (Posti)
    int nof_wk_seats_primi;
    int nof_wk_seats_secondi;
    int nof_wk_seats_coffee;
    int nof_wk_seats_cassa;
    int nof_table_seats;

    // Logistica Cibo & Versione Completa
    int avg_refill_primi;
    int avg_refill_secondi;
    int max_porzioni_primi;
    int max_porzioni_secondi;
    int avg_refill_time;
    int stop_duration;
    int n_new_users;

} conf_t;


static void
load_config(
    const char   *filename,
          conf_t *conf
) {
   
    FILE *file = NULL;
    char *json_content = NULL;
    long file_size = 0;
    cJSON *json = NULL;
    int status = 0; // 0 = OK, -1 = Errore parziale

    // 1. Apertura File
    file = zfopen(filename, "rb");
    file_size = zfsize(file);
    
    json_content = (char *)malloc(file_size + 1);
    if (json_content == NULL) {
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

    // 5. Estrazione Valori usando la Macro
    PARSE_INT(json, "SIM_DURATION", sim_duration);
    PARSE_INT(json, "N_NANO_SECS", n_nano_secs);
    PARSE_INT(json, "OVERLOAD_THRESHOLD", overload_threshold);
    
    PARSE_INT(json, "NOF_WORKERS", nof_workers);
    PARSE_INT(json, "NOF_USERS", nof_users);
    PARSE_INT(json, "MAX_USERS_PER_GROUP", max_users_per_group);
    PARSE_INT(json, "NOF_PAUSE", nof_pause);

    PARSE_INT(json, "AVG_SRVC_PRIMI", avg_srvc_primi);
    PARSE_INT(json, "AVG_SRVC_MAIN_COURSE", avg_srvc_main_course);
    PARSE_INT(json, "AVG_SRVC_COFFEE", avg_srvc_coffee);
    PARSE_INT(json, "AVG_SRVC_CASSA", avg_srvc_cassa);

    PARSE_INT(json, "NOF_WK_SEATS_PRIMI", nof_wk_seats_primi);
    PARSE_INT(json, "NOF_WK_SEATS_SECONDI", nof_wk_seats_secondi);
    PARSE_INT(json, "NOF_WK_SEATS_COFFEE", nof_wk_seats_coffee);
    PARSE_INT(json, "NOF_WK_SEATS_CASSA", nof_wk_seats_cassa);
    PARSE_INT(json, "NOF_TABLE_SEATS", nof_table_seats);

    PARSE_INT(json, "AVG_REFILL_PRIMI", avg_refill_primi);
    PARSE_INT(json, "AVG_REFILL_SECONDI", avg_refill_secondi);
    PARSE_INT(json, "MAX_PORZIONI_PRIMI", max_porzioni_primi);
    PARSE_INT(json, "MAX_PORZIONI_SECONDI", max_porzioni_secondi);
    PARSE_INT(json, "AVG_REFILL_TIME", avg_refill_time);
    PARSE_INT(json, "STOP_DURATION", stop_duration);
    PARSE_INT(json, "N_NEW_USERS", n_new_users);

    // 6. Pulizia
    cJSON_Delete(json);
    free(json_content);
}

#endif
