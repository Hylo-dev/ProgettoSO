//
// Created by Eliomar Alejandro Rodriguez Ferrer on 12/12/25.
//

/**
 * @file dict.c
 * @brief Implementation of the dict_t using open addressing and incremental rehashing.
 * @author Eliomar Alejandro Rodriguez Ferrer
 * @date 12/12/25
 *
 * This implementation uses:
 * - Open addressing for collision resolution.
 * - A hybrid probing sequence (Quadratic for the first 32 attempts, then Linear) to reduce clustering.
 * - Incremental rehashing to distribute the resizing cost over multiple operations.
 * - Atomic reference counting for memory management.
 */

#include "dict.h"

#include <stdlib.h>

#ifndef likely
#define likely(x)   __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)
#endif

#define MIN_SIZE 1024
#define RANGE_RESIZE 0.60

// 3 bit for EMPTY(0), USED(1), DELETED(2) state
#define STATE_MASK  0x7ULL          // 00...00111
#define PTR_MASK    (~STATE_MASK)   // 11...11000

// Get a clean key
#define GET_KEY(tagged_ptr)  ((void*)((uintptr_t)(tagged_ptr) & PTR_MASK))

// Get state from a pointer
#define GET_STATE(tagged_ptr) ((NodeState)((uintptr_t)(tagged_ptr) & STATE_MASK))

// Make a pointer "dirty" (Key + State)
#define TAG_PTR(key, state)  ((void*)((uintptr_t)(key) | (state)))

// MARK: - HELPER FUNCTIONS

/**
 * @brief Searches for a value associated with a key in a specific internal array.
 * @internal
 *
 * Uses a hybrid probing strategy:
 * 1. Quadratic probing `(i*i)` for the first 32 attempts to spread out clusters.
 * 2. Linear probing `(+i)` thereafter to ensure all slots are eventually visited.
 * Includes CPU cache prefetching optimization.
 *
 * @param self The dict_t instance.
 * @param array The specific array bucket to search (primary or old/rehashing).
 * @param key The key to look for.
 * @return The data associated with the key, or NULL if not found.
 */
static inline any
_dict_search(
    dict_t* self,
    Array* array,
    let_any key
) {
    if (unlikely(!array->elements || array->size == 0)) return NULL;

    // size_t index = self->hash(key) % array->size;
    const size_t hash_value = self->hash(key);
    size_t index = hash_value & (array->size - 1); // This work because the size is pow of 2
    const size_t start_idx = index;
    size_t i = 0;

    do {
        // Change to quadratic clusters
        // index = (index + 1) % array->size;

        const size_t next_i = i + 1;
        size_t next_index;
        if (i < 32) {
            next_index = (start_idx + next_i*next_i) & (array->size - 1);

        } else { next_index = (start_idx + next_i) & (array->size - 1); }

        __builtin_prefetch(&array->elements[index], 0, 1);

        any raw_tagged = array->elements[index].key;

        NodeState state = GET_STATE(raw_tagged);

        if (unlikely(state == EMPTY)) return NULL;

        any current_key = GET_KEY(raw_tagged);

        if (likely(state == USED && self->compare(key, current_key) == 0))
            return array->elements[index].data;

        index = next_index;
        i++;
    } while (i < array->size); // index != start_idx

    return NULL;
}

/**
 * @brief Changes the state of a node (e.g., to DELETED) in a specific array.
 * @internal
 *
 * @param self The dict_t instance.
 * @param array The specific array to modify.
 * @param key The key to locate.
 * @param state The new state to apply (usually DELETED).
 * @return true if the node was found and updated, false otherwise.
 */
static inline bool
_dict_change_state(
    dict_t* self,
    Array* array,
    let_any key,
    NodeState state
) {
    if (unlikely(!array->elements || array->size == 0)) return false;

    const size_t hash_value = self->hash(key);
    size_t index = hash_value & (array->size - 1); // self->hash(key) % array->size;
    const size_t start_idx = index;
    size_t i = 0;

    do {
        // Change to quadratic clusters
        // index = (index + 1) % array->size;
        const size_t next_i = i + 1;
        size_t next_index;
        if (i < 32) {
            next_index = (start_idx + next_i*next_i) & (array->size - 1);

        } else { next_index = (start_idx + next_i) & (array->size - 1); }

        __builtin_prefetch(&array->elements[index], 0, 1);

        any raw_tagged = array->elements[index].key;
        NodeState state_tagged = GET_STATE(raw_tagged);

        if (unlikely(state_tagged == EMPTY)) return false;

        any current_key = GET_KEY(raw_tagged);

        if (likely(state_tagged == USED && self->compare(key, current_key) == 0)) {
            array->elements[index].key = TAG_PTR(current_key, state);
            // element->state = state;
            return true;
        }

        index = next_index;
        i++;
    } while (i < array->size);

    return false;
}

/**
 * @brief Inserts or updates an element in a specific array.
 * @internal
 *
 * It looks for:
 * 1. An existing key (to update).
 * 2. An EMPTY slot (to insert).
 * 3. A DELETED slot (to recycle, if key not found elsewhere).
 *
 * @param self The dict_t instance.
 * @param array The specific array to insert into.
 * @param element The element structure containing key, data, and state.
 * @return DICT_ADDED on new insertion, DICT_UPDATED on update, DICT_ERR on failure.
 */
static inline DictResult
_dict_insert(
    dict_t* self,
    Array* array,
    struct DictElement element
) {

    if (unlikely(!array->elements)) return DICT_ERR;

    ssize_t best_position = -1;
    const size_t hash_value = self->hash(element.key);
    size_t index = hash_value & (array->size - 1);
    const size_t start_idx = index;
    size_t i = 0;

    do {
        // Change to quadratic clusters
        // index = (index + 1) % array->size;
        const size_t next_i = i + 1;
        size_t next_index;
        if (i < 32) {
            next_index = (start_idx + next_i*next_i) & (array->size - 1);

        } else { next_index = (start_idx + next_i) & (array->size - 1); }

        __builtin_prefetch(&array->elements[index], 0, 1);

        any raw_tagged = array->elements[index].key;
        NodeState state_tagged = GET_STATE(raw_tagged);

        if (unlikely(state_tagged == EMPTY)) {
            if (best_position == -1) best_position = (ssize_t)index;

            array->elements[best_position].key  = TAG_PTR(element.key, USED);
            array->elements[best_position].data = element.data;

            // array->elements[best_position] = element;
            // array->elements[best_position].state = USED;
            return DICT_ADDED;
        }

        any current_key = GET_KEY(raw_tagged);

        if (likely(state_tagged == USED && self->compare(element.key, current_key) == 0)) {
            array->elements[index].key  = TAG_PTR(element.key, USED);
            array->elements[index].data = element.data;

            // element_array->state = USED;
            return DICT_UPDATED;
        }

        if (state_tagged == DELETED) {
            if (best_position == -1) best_position = (ssize_t)index;
        }

        index = next_index;
        i++;
    } while (i < array->size);

    if (best_position != -1) {

        array->elements[best_position].key  = TAG_PTR(element.key, USED);
        array->elements[best_position].data = element.data;

        // array->elements[best_position] = element;
        // array->elements[best_position].state = USED;
        return DICT_ADDED;
    }

    return DICT_ERR;
}

/**
 * @brief Inserts element in a specific array.
 * @internal
 *
 * Insert directly into first space on array
 *
 * @param self The dict_t instance.
 * @param array The specific array to insert into.
 * @param element The element structure containing key, data, and state.
 */
static inline void
_dict_insert_blind(
    dict_t* self,
    Array* array,
    struct DictElement element
) {

    const size_t hash_value = self->hash(element.key);
    size_t index = hash_value & (array->size - 1);
    const size_t start_idx = index;
    size_t i = 0;

    do {
        // Change to quadratic clusters
        // index = (index + 1) % array->size;
        const size_t next_i = i + 1;
        size_t next_index;
        if (i < 32) {
            next_index = (start_idx + next_i*next_i) & (array->size - 1);

        } else { next_index = (start_idx + next_i) & (array->size - 1); }

        __builtin_prefetch(&array->elements[index], 0, 1);

        any raw_tagged = array->elements[index].key;
        NodeState state_tagged = GET_STATE(raw_tagged);

        if (unlikely(state_tagged == EMPTY)) {
            array->elements[index].key  = TAG_PTR(element.key, USED);
            array->elements[index].data = element.data;

            // array->elements[index] = element;
            // array->elements[index].state = USED;

            array->num_elements++;
            return;
        }

        index = next_index;
        i++;
    } while (i < array->size);
}

/**
 * @brief Finds the raw index of a key in the array.
 * @internal
 *
 * @param self The dict_t instance.
 * @param array The specific array to search.
 * @param key The key to find.
 * @return The index of the key if found, -1 otherwise.
 */
static inline ssize_t
_dict_find_index(
    dict_t* self,
    Array* array,
    let_any key
) {

    if (unlikely(!array->elements || array->num_elements == 0)) return -1;

    const size_t hash_value = self->hash(key);
    size_t index = hash_value & (array->size - 1); // self->hash(key) % array->size;
    const size_t start_idx = index;
    size_t i = 0;

    do {
        // Change to quadratic clusters
        // index = (index + 1) % array->size;
        const size_t next_i = i + 1;
        size_t next_index;
        if (i < 32) {
            next_index = (start_idx + next_i*next_i) & (array->size - 1);

        } else { next_index = (start_idx + next_i) & (array->size - 1); }

        __builtin_prefetch(&array->elements[index], 0, 1);

        any raw_tagged = array->elements[index].key;
        NodeState state_tagged = GET_STATE(raw_tagged);

        if (unlikely(state_tagged == EMPTY)) return -1;

        any current_key = GET_KEY(raw_tagged);

        if (likely(state_tagged == USED && self->compare(key, current_key) == 0)) {
            return (ssize_t)index;
        }

        index = next_index;
        i++;
    } while (i < array->size);

    return -1;
}

/**
 * @brief Performs an incremental step of the rehashing process.
 * @internal
 *
 * Moves a limited number of elements (bucket by bucket) from array[0] (old)
 * to array[1] (new). This prevents locking the dict_t for a long time
 * during resizing.
 *
 * @param self The dict_t instance.
 */
static inline void
dict_rehash_table(dict_t* self) {
    size_t moved_count = 0;

    __builtin_prefetch(&self->array[0].elements[self->rehash_index], 0, 1);
    while (
        self->rehash_index < self->array[0].size &&
        moved_count < 100
    ) {
        struct DictElement* element = &self->array[0].elements[self->rehash_index];
        any raw_tagged = element->key;
        NodeState state_tagged = GET_STATE(raw_tagged);

        if (state_tagged != USED) {
            self->rehash_index++;
            continue;
        }

        any clean_key = GET_KEY(raw_tagged);

        struct DictElement clean_element = {
            .key = clean_key,
            .data = element->data
        };

        _dict_insert_blind(self, &self->array[1], clean_element);

        element->key = TAG_PTR(clean_key, DELETED);
        self->array[0].num_elements--;
        moved_count++;

        self->rehash_index++;
    }

    if (self->array[0].num_elements == 0) {
        free(self->array[0].elements);

        self->array[0] = self->array[1];
        self->array[1] = (Array){ NULL, 0, 0};

        self->is_rehashing = false;
        self->rehash_index = 0;
    }
}




// MARK: - dict_t FUNCTIONS




/**
 * @brief Initializes a new dict_t.
 *
 * Allocates memory for the primary array and sets initial parameters.
 *
 * @param hash Function pointer for key hashing.
 * @param compare Function pointer for key comparison.
 * @return A new dict_t pointer or NULL on allocation failure.
 */
dict_t*
init_dict(
    size_t (*hash)   (let_any key),
    int    (*compare)(let_any a, let_any b)
) {
    dict_t *dict = zmalloc(sizeof(*dict));

    // Init Arrays on dict_t
    dict->array[0].elements     = zcalloc(MIN_SIZE, sizeof(struct DictElement));
    dict->array[0].size         = MIN_SIZE;
    dict->array[0].num_elements = 0;

    dict->array[1].elements     = NULL;
    dict->array[1].size         = 0;
    dict->array[1].num_elements = 0;

    // Init dict
    dict->rehash_index          = 0;
    dict->is_rehashing          = false;
    dict->hash                  = hash;
    dict->compare               = compare;

    // Start reference counting
    atomic_init(&dict->ref_count, 1);

    return dict;
}

/**
 * @brief Increments the reference count of the dict_t.
 *
 * @param self The dict_t instance.
 * @return The dict_t instance.
 */
dict_t*
retain_dict(dict_t* self) {
    if (unlikely(!self)) return NULL;

    atomic_fetch_add(&self->ref_count, 1);

    return self;
}

/**
 * @brief Decrements the reference count and frees memory if count reaches zero.
 *
 * Uses explicit memory ordering to ensure thread safety during destruction.
 *
 * @param self The dict_t instance.
 */
void
release_dict(dict_t* self) {
    if (unlikely(!self)) return;

    const size_t old_ref_count = (size_t)atomic_fetch_sub_explicit(
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

/**
 * @brief Retrieves a value from the dict_t.
 *
 * Performs a rehash step if necessary. Checks the secondary array (array[1])
 * if a rehash is in progress, then the primary array.
 *
 * @param self The dict_t instance.
 * @param key The key to lookup.
 * @return The value associated with the key, or NULL.
 */
any
dict_get(
    dict_t* self,
    let_any key
) {
    if (unlikely(!self)) return NULL;

    if (self->is_rehashing) { dict_rehash_table(self); }

    if (self->is_rehashing) {
        any result = _dict_search(self, &self->array[1], key);
        if (result) return result;
    }

    return _dict_search(self, &self->array[0], key);
}

/**
 * @brief Sets a value for a key in the dict_t.
 *
 * - Triggers a table resize if load factor exceeds threshold (0.75).
 * - Handles insertion into the new array if rehashing is active.
 * - Ensures no duplicates exist in the old array during rehash.
 *
 * @param self The dict_t instance.
 * @param key The key to set.
 * @param value The value to associate.
 * @return true on success, false on failure.
 */
bool
dict_set(
    dict_t* self,
    let_any key,
    let_any value
) {
    if (unlikely(!self)) return false;

    if (self->is_rehashing) {
        dict_rehash_table(self);

    } else if (self->array[0].num_elements >= (size_t)((double)self->array[0].size * RANGE_RESIZE)) {
        const size_t new_size = self->array[0].size * 2;

        self->array[1].elements     = zcalloc(new_size, sizeof(struct DictElement));
        self->array[1].size         = new_size;
        self->array[1].num_elements = 0;

        self->is_rehashing = true;
        self->rehash_index = 0;
    }

    const struct DictElement element = { (any)key, (any)value };

    if (self->is_rehashing && self->array[1].num_elements > 0) {

        const ssize_t new_idx = _dict_find_index(self, &self->array[1], key);
        const ssize_t old_idx = _dict_find_index(self, &self->array[0], key);

        const DictResult result = _dict_insert(self, &self->array[1], element);

        if (result != DICT_ERR) {

            if (result == DICT_ADDED && new_idx == -1) self->array[1].num_elements++;

            if (old_idx != -1) {
                self->array[0].elements[old_idx].key = TAG_PTR(self->array[0].elements[old_idx].key, DELETED);

                // self->array[0].elements[old_idx].state = DELETED;
                self->array[0].num_elements--;
            }

            return true;
        }

        return false;
    }

    const DictResult result = _dict_insert(self, &self->array[0], element);
    if (result != DICT_ERR) {
        if (result == DICT_ADDED) self->array[0].num_elements++;

        return true;
    }

    return false;
}

/**
 * @brief Removes a key from the dict_t.
 *
 * Marks the node as DELETED. If rehashing is active, checks both arrays.
 *
 * @param self The dict_t instance.
 * @param key The key to remove.
 * @return true if removed, false if not found.
 */
bool
dict_remove(
    dict_t* self,
    let_any key
) {
    if (unlikely(!self)) return false;

    if (self->is_rehashing) { dict_rehash_table(self); }

    if (self->is_rehashing && self->array[1].num_elements > 0) {
        if (_dict_change_state(self, &self->array[1], key, DELETED)) {
            self->array[1].num_elements--;
            return true;
        }
    }

    if (_dict_change_state(self, &self->array[0], key, DELETED)) {
        self->array[0].num_elements--;
        return true;
    }


    return false;
}
