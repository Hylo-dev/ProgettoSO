//
// Created by Eliomar Alejandro Rodriguez Ferrer on 08/01/26.
//

#ifndef _MENU_H
#define _MENU_H

#include "objects.h"
#include "tools.h"
#include "cJSON.h"
#include <string.h>
#include <stdio.h>


static const char* dish_names[] = {
    "main",
    "first",
    "coffee"
};

static char*
read_file(const char* filename) {
    FILE *file = zfopen(filename, "rb");

    const long length = zfsize(file);

    char *buffer = (char*)zmalloc((size_t)length + 1);

    fread(buffer, 1, (size_t)length, file);
    buffer[length] = '\0';

    fclose(file);
    return buffer;
}

static void
load_category(
    const cJSON    *json,
    const dish_type type,
          dish_t   *target_array,
          size_t   *size_array,
    const size_t    max_dishes
) {
    cJSON *array = cJSON_GetObjectItemCaseSensitive(json, dish_names[type]);
    if (!array) return;

    size_t count = (size_t)cJSON_GetArraySize(array);
    if (count > max_dishes) count = max_dishes;

    *size_array = count;

    for (size_t i = 0; i < count; i++) {
        const cJSON *item   = cJSON_GetArrayItem(array, (int)i);

        const cJSON *id    = cJSON_GetObjectItem(item, "id");
        const cJSON *name  = cJSON_GetObjectItem(item, "name");
        const cJSON *price = cJSON_GetObjectItem(item, "price");
        const cJSON *time  = cJSON_GetObjectItem(item, "time");

        if (id && name && price && time) {
            target_array[i].id          = (size_t)id->valueint;
            target_array[i].price       = (size_t)price->valueint;
            target_array[i].eating_time = (size_t)time->valueint;

            strncpy(target_array[i].name, name->valuestring, DISH_NAME_MAX_LEN - 1);
            target_array[i].name[DISH_NAME_MAX_LEN - 1] = '\0';
        }
    }
}

static void
load_menu(
    const char *filename,
    simctx_t   *ctx
) {
    char* raw_json = read_file(filename);

    cJSON* json = cJSON_Parse(raw_json);
    if (!json) {
        free(raw_json);
        free(json);
        panic("ERROR: Failed to parse JSON");
    }

    load_category(json, MAIN, ctx->menu[MAIN].data, &ctx->menu[MAIN].size, MAX_ELEMENTS);
    load_category(json, FIRST, ctx->menu[FIRST].data, &ctx->menu[FIRST].size, MAX_ELEMENTS);
    load_category(json, COFFEE, ctx->menu[COFFEE].data, &ctx->menu[COFFEE].size, MAX_ELEMENTS);

    cJSON_Delete(json);
    free(raw_json);
}


#endif //PROGETTOSO_MENU_H
