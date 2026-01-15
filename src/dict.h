//
// Created by Eliomar Alejandro Rodriguez Ferrer on 12/12/25.
//

/**
 * @file dict.h
 * @brief Header file for a generic Dictionary (Hash Map) implementation.
 *
 * This dictionary supports reference counting for memory management and
 * appears to implement incremental rehashing.
 * It relies on an external "tool.h" for the `any` and `let_any` type definitions.
 */

#ifndef DICT_DICT_H
#define DICT_DICT_H

#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "tools.h"

/**
 * @enum NodeState
 * @brief Represents the state of a specific slot in the hash map.
 * Used for open addressing collision resolution.
 */
typedef enum : uint8_t {
    EMPTY   = 0, /**< The slot is empty and has never been used. */
    USED    = 1, /**< The slot is currently occupied by valid data. */
    DELETED = 2, /**< The slot previously held data but was removed (tombstone). */
} NodeState;

/**
 * @enum DictResult
 * @brief Return codes for dictionary operations.
 */
typedef enum : uint8_t {
    DICT_ERR = 0, /**< An error occurred. */
    DICT_ADDED,   /**< A new key-value pair was successfully added. */
    DICT_UPDATED  /**< An existing key was found and its value was updated. */
} DictResult;

/**
 * @struct Array
 * @brief Represents the internal storage array of the dictionary.
 */
typedef struct Array {
    /**
     * @struct DictElement
     * @brief Internal bucket structure.
     * Aligned to 64 bytes to match common CPU cache lines, reducing false sharing.
     */
    struct DictElement {
        any key;  /**< The key associated with the element. */
        any data; /**< The value stored in the element. */
        // NodeState state; // Removed for pointer tagging optimization
    } *elements;

    size_t size;         /**< Total capacity of the array. */
    size_t num_elements; /**< Current number of occupied elements. */
} Array;

/**
 * @struct Dictionary
 * @brief The main dictionary object structure.
 *
 * Contains two arrays to support incremental rehashing, allowing the dictionary
 * to resize without blocking the main execution thread for a long period.
 */
typedef struct {
    /**
     * @brief Storage arrays.
     * Index 0 is the primary array. Index 1 is the old array during rehashing.
     */
    Array       array[2];
    atomic_int  ref_count;    /**< Atomic reference counter for memory management. */
    size_t      rehash_index; /**< Tracks the current progress of the rehashing process. */
    bool        is_rehashing; /**< Flag indicating if a rehash is currently in progress. */

    /**
     * @brief Function pointer to hash a key.
     */
    size_t (*hash)(let_any key);

    /**
     * @brief Function pointer to compare two keys.
     */
    int (*compare)(let_any a, let_any b);

} dict_t;

// - MARK: Functions prototypes

/**
 * @brief Initializes a new Dictionary instance.
 */
dict_t *
init_dict(
    size_t (*hash)(let_any key),
    int (*compare)(let_any a, let_any b)
);

/**
 * @brief Retains the dictionary (increments reference count).
 */
dict_t *
retain_dict(
    dict_t *self
);

/**
 * @brief Releases the dictionary (decrements reference count).
 */
void
release_dict(
    dict_t* self
);

/**
 * @brief Retrieves a value from the dictionary.
 */
any
dict_get(
    dict_t* self,
    let_any key
);

/**
 * @brief Inserts or updates a value in the dictionary.
 */
bool
dict_set(
    dict_t* self,
    let_any key,
    let_any value
);

/**
 * @brief Removes a key-value pair from the dictionary.
 */
bool
dict_remove(
    dict_t *self,
    let_any key
);

#endif // DICT_DICT_H
