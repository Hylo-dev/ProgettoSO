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
    Dictionary *dict = malloc(sizeof(*dict));
    if (!dict) return NULL;

    // Init Arrays on dictionary
    dict->array[0].elements = malloc(MIN_SIZE * sizeof(dict->array[0]));
    if (!dict->array[0].elements) {
        free(dict);
        return NULL;
    }
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
retain_dict(Dictionary* self);

void
release_dict(Dictionary* self);

any
dict_get(
    Dictionary* self,
    let_any key
);

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