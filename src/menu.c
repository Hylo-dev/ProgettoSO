//
// Created by Eliomar Alejandro Rodriguez Ferrer on 08/01/26.
//

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
    if (!file) return NULL;

    fseek(file, 0, SEEK_END);
    const long length = ftell(file);
    fseek(file, 0, SEEK_SET);

    char *buffer = malloc(length + 1);
    if (buffer) {
        fread(buffer, 1, length, file);
        buffer[length] = '\0';
    }

    fclose(file);
    return buffer;
}

void
load_category(
    const cJSON* json,
    const dish_type type,
          dish_t *target_array,
          size_t *size_array,
    const size_t max_dishes
) {
    cJSON *array = cJSON_GetObjectItemCaseSensitive(json, dish_names[type]);
    if (!array) return;

    int count = cJSON_GetArraySize(array);
    if (count > max_dishes) count = max_dishes;

    *size_array = count;

    for (size_t i = 0; i < count; i++) {
        const cJSON *item   = cJSON_GetArrayItem(array, i);

        const cJSON *id    = cJSON_GetObjectItem(item, "id");
        const cJSON *name  = cJSON_GetObjectItem(item, "name");
        const cJSON *price = cJSON_GetObjectItem(item, "price");
        const cJSON *time  = cJSON_GetObjectItem(item, "time");

        if (id && name && price && time) {
            target_array[i].id          = id->valueint;
            target_array[i].price       = price->valueint;
            target_array[i].eating_time = time->valueint;

            strncpy(target_array[i].name, name->valuestring, DISH_NAME_MAX_LEN - 1);
            target_array[i].name[DISH_NAME_MAX_LEN - 1] = '\0';
        }
    }
}

void
load_menu(
    char* filename,
    sim_ctx_t* data
) {
    char* raw_json = read_file(filename);
    if (!raw_json) panic("ERROR: Read json failed");

    cJSON* json = cJSON_Parse(raw_json);
    if (!json) {
        free(json);
        free(filename);
        panic("ERROR: Failed to parse JSON");
    }

    load_category(json, MAIN, data->main_courses_menu, &data->main_menu_size, MAX_MAIN_COURSES);
    load_category(json, FIRST, data->first_courses_menu, &data->first_menu_size, MAX_FIRST_COURSES);
    load_category(json, COFFEE, data->coffee_menu, &data->coffee_menu_size, MAX_COFFEE_DISHES);

    cJSON_Delete(json);
    free(raw_json);
}
