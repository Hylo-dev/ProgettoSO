#ifndef QUEUE_H
#define QUEUE_H

#include <stddef.h>
#include <stdlib.h>
#include <stdbool.h>
#include "tools.h"

typedef struct {
    any    base;
    size_t head;
    size_t tail;
    size_t cap;
    size_t count;
    size_t width;
    size_t refcnt;
} queue_r, *queue;

typedef const queue_r* queue_c;

queue
queue_init   (size_t capacity, size_t elem_size);

queue
queue_retain (queue self);

void
queue_release(queue self);

size_t
queue_size(queue self);

bool
queue_push(queue self, any elem);

bool
queue_pop (queue self, any out_buff);

any_c
queue_peek(queue self);


#endif
