//
// Created by Eliomar Alejandro Rodriguez Ferrer on 12/12/25.
//

#ifndef DICT_DICT_H
#define DICT_DICT_H

#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "tool.h"

typedef enum: uint8_t {
    EMPTY   = 0,
    USED    = 1,
    DELETED = 2,
} NodeState;

typedef struct Array {
    struct DictElement {
        any key;
        any data;

        NodeState state;
    } *elements;

    size_t size;
    size_t num_elements;

} Array;

typedef struct {
    Array array[2];

    atomic_int ref_count;

    size_t rehash_index;
    bool is_rehashing;

    size_t (*hash)(let_any key);
    int (*compare)(let_any a, let_any b);
} Dictionary;

// - MARK: Functions prototypes

Dictionary*
init_dict(
    size_t (*hash)   (let_any key),
    int    (*compare)(let_any a, let_any b)
);

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

#endif //DICT_DICT_H