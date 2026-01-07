#include "../libds/queue.h"
#include "tools.h"
#include <stddef.h>
#include <string.h>
#include <stdlib.h>

#define AT(idx) ((char*)self->base + ((idx) * self->width))

queue
queue_init(
    size_t capacity,
    size_t elem_size
) {
    queue q  = xmalloc(sizeof(*q));

    q->cap    = capacity;
    q->count  = 0;
    q->head   = 0;
    q->tail   = 0;
    q->refcnt = 1;
    q->width  = elem_size;
    q->base   = xmalloc(elem_size*capacity);

    return q;
}

queue
queue_retain(
    queue self
){
    if (!self) return NULL;
    self->refcnt++;
    return self;
}

void
queue_release(
    queue self
){
    if (!self) return;
    self->refcnt--;
    if (self->refcnt == 0) {
        free(self->base);
        free(self);
    }
}

size_t
queue_size(
    queue self
) {
    if(!self) return 0;
    return self->count;
}

bool
queue_push(
    queue self,
    any elem
) {
    if (!self || !elem || self->count == self->cap) return false;

    memcpy(
        AT(self->head),
        elem,
        self->width
    );
    self->count++;
    self->head = (self->head+1) % self->cap;

    return true;
}

bool
queue_pop(
    queue self,
    any   out_buff
) {
    if (!self || self->count == 0 || !out_buff) return false;

    memcpy(out_buff, AT(self->tail), self->width);

    self->count--;
    self->tail = (self->tail + 1) % self->cap;

    return true;
}

any_c
queue_peek(
    queue self
) {
    if (!self || self->count==0) return NULL;
    return AT(self->tail);
}

#undef AT
