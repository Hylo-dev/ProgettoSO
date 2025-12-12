//
// Created by Eliomar Alejandro Rodriguez Ferrer on 12/12/25.
//

#ifndef DICT_TOOL_H
#define DICT_TOOL_H
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

static inline
void* zmalloc(const size_t size) {
    void *ptr = malloc(size);

    if (!ptr) {
        fprintf(stderr, "ERROR: Malloc failed\n");
        exit(EXIT_FAILURE);
    }

    return ptr;
}

static inline
void* zcalloc(
    const size_t new_size,
    const size_t size
) {
    void *ptr = calloc(new_size, size);
    if (!ptr) {
        fprintf(stderr, "ERROR: Calloc failed\n");
        exit(EXIT_FAILURE);
    }

    return ptr;
}

typedef void* any;
typedef const void* let_any;

#endif //DICT_TOOL_H
