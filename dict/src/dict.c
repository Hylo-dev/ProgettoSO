//
// Created by Eliomar Alejandro Rodriguez Ferrer on 12/12/25.
//

#include "include/dict.h"

#include <stdlib.h>

#define MIN_SIZE 10

Dictionary*
init_dict(
    size_t (*hash)   (let_any key),
    int    (*compare)(let_any a, let_any b)
) {
    Dictionary *dict = zmalloc(sizeof(*dict));

    // Init Arrays on dictionary
    dict->array[0].elements     = zmalloc(MIN_SIZE * sizeof(dict->array[0]));
    dict->array[0].size         = MIN_SIZE;
    dict->array[0].num_elements = 0;
    dict->array[1].elements     = NULL;

    // Init dict
    dict->rehash_index          = 0;
    dict->is_rehashing          = false;
    dict->hash                  = hash;
    dict->compare               = compare;

    // Start reference counting
    atomic_init(&dict->ref_count, 1);

    return dict;
}

Dictionary*
retain_dict(Dictionary* self) {
    if (!self) return NULL;

    atomic_fetch_add(&self->ref_count, 1);

    return self;
}

void
release_dict(Dictionary* self) {
    if (!self) return;

    const size_t old_ref_count = atomic_fetch_sub_explicit(
        &self->ref_count,
        1,
        memory_order_relaxed
    );

    if (old_ref_count == 1) {
        atomic_thread_fence(memory_order_acquire);

        free(self->array[0].elements);
        if (self->array[1].elements) free(self->array[1].elements);

        free(self);
    }
}

any
dict_get(
    Dictionary* self,
    let_any key
) {
    if (!self) return NULL;

    if (self->is_rehashing && self->array[1].elements && self->array[1].num_elements != 0) {

        size_t index = self->hash(key) % self->array[1].size;
        const size_t start_idx = index;

        do {
            const struct DictElement element = self->array[1].elements[index];

            if (element.state == EMPTY) return NULL;

            if (element.state == USED && self->compare(key, element.key) == 0)
                return element.data;

            index = (index + 1) % self->array[1].size;
        } while (index != start_idx);
    }
}

bool
dict_set(
    Dictionary* self,
    let_any key,
    let_any value
);

bool
dict_remove(
    Dictionary* self,
    let_any key
);