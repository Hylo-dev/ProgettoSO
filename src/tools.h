#ifndef TOOLS_H
#define TOOLS_H

#include <stdlib.h>
#include <stdio.h>
#include <stddef.h>

// any type
typedef       void* any; 

// const any type
typedef const void* any_c; 

#define foreach(DECL, ARRAY_OF_PTRS, COUNT) \
    for (size_t _fe_i = 0; _fe_i < (COUNT); _fe_i++) \
        for (DECL = (ARRAY_OF_PTRS)[_fe_i], * _fe_once = (void*)1; (size_t)_fe_once; _fe_once = (void*)0)


static inline any
xmalloc(size_t size) {
    any res = malloc(size);
    if (!res) {
        fprintf(stderr, "ERROR: malloc failed");
        exit(1);
    }
    return res;
}

static inline any
xrealloc(void* old_ptr, size_t size) {
    any res = realloc(old_ptr, size);
    if (!res) {
        fprintf(stderr, "ERROR: realloc failed");
        exit(1);
    }
    return res;
}

#endif
