//
// Created by Eliomar Alejandro Rodriguez Ferrer on 08/01/26.
//

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "objects.h"
#include "cJSON.h"
#include "tools.h"

const char* dish_names[] = {
    "main",
    "first",
    "side",
    "coffee"
};

static char*
read_file(const char* filename) {
    FILE *file = fopen(filename, "rb");
    if (!file) panic("ERROR: Unable to read the file %s", filename);

    fseek(file, 0, SEEK_END);
    const long length = ftell(file);
    fseek(file, 0, SEEK_SET);

    char *buffer = zmalloc((size_t)length + 1);

    fread(buffer, 1, (size_t)length, file);
    buffer[length] = '\0';

    fclose(file);
    return buffer;
}

void
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

void
load_menu(
    char     *filename,
    simctx_t *ctx
) {
    char* raw_json = read_file(filename);

    cJSON* json = cJSON_Parse(raw_json);
    if (!json) {
        free(raw_json);
        free(json);
        panic("ERROR: Failed to parse JSON");
    }

    load_category(json, MAIN, ctx->main_courses_menu, &ctx->main_menu_size, MAX_MAIN_COURSES);
    load_category(json, FIRST, ctx->first_courses_menu, &ctx->first_menu_size, MAX_FIRST_COURSES);
    load_category(json, COFFEE, ctx->coffee_menu, &ctx->coffee_menu_size, (int)MAX_COFFEE_DISHES);

    cJSON_Delete(json);
    free(raw_json);
}
