#ifndef _TOOLS_H
#define _TOOLS_H

#include <errno.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <stddef.h>
#include <sys/msg.h>
#include <sys/_types/_key_t.h>

// any type
typedef       void* any; 

// const any type
typedef const void* let_any; 

#define foreach(DECL, ARRAY_OF_PTRS, COUNT) \
    for (size_t _fe_i = 0; _fe_i < (COUNT); _fe_i++) \
        for (DECL = (ARRAY_OF_PTRS)[_fe_i], * _fe_once = (void*)1; (size_t)_fe_once; _fe_once = (void*)0)

static inline void
panic(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);

    exit(errno);
}

static inline any
zmalloc(size_t size) {
    any ptr = malloc(size);
    if (!ptr)   panic("ERROR: Malloc failed\n");
    return ptr;
}

static inline any
zrealloc(
    any    old_ptr,
    size_t size
) {
    any ptr = realloc(old_ptr, size);
    if (!ptr) panic("ERROR: Calloc failed\n");
    return ptr;
}

static inline any
zcalloc(
    size_t new_size,
    size_t size
) {
    any ptr = calloc(new_size, size);
    if (!ptr) panic("ERROR: Calloc failed\n");
    return ptr;
}

// ====================== SHM WRAPPER ======================
static inline size_t
zmsgget(
    const key_t key,
    const int   mode
) {
    int result = msgget(key, mode);
    if (result < 0)
        panic("ERROR: Creation message queue is failed\n");

    return (size_t)result;
}


#endif
