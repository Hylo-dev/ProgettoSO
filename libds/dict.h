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

#include "tool.h"

/**
 * @enum NodeState
 * @brief Represents the state of a specific slot in the hash map.
 * Used for open addressing collision resolution.
 */
typedef enum: uint8_t {
    EMPTY   = 0, /**< The slot is empty and has never been used. */
    USED    = 1, /**< The slot is currently occupied by valid data. */
    DELETED = 2, /**< The slot previously held data but was removed (tombstone). */
} NodeState;

/**
 * @enum DictResult
 * @brief Return codes for dictionary operations.
 */
typedef enum: uint8_t {
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
        any key;        /**< The key associated with the element. */
        any data;       /**< The value stored in the element. */

        NodeState state; /**< Metadata regarding the slot's occupancy. */
    } *elements; // Remove force padding because in big data the cache miss is most frequently

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
    Array array[2];

    atomic_int ref_count; /**< Atomic reference counter for memory management. */

    size_t rehash_index;  /**< Tracks the current progress of the rehashing process. */
    bool is_rehashing;    /**< Flag indicating if a rehash is currently in progress. */

    /**
     * @brief Function pointer to hash a key.
     * @param key The key to hash.
     * @return The calculated hash value.
     */
    size_t (*hash)(let_any key);

    /**
     * @brief Function pointer to compare two keys.
     * @param a The first key.
     * @param b The second key.
     * @return 0 if equal, non-zero otherwise.
     */
    int (*compare)(let_any a, let_any b);
} Dictionary;

// - MARK: Functions prototypes

/**
 * @brief Initializes a new Dictionary instance.
 *
 * Allocates memory for the dictionary and sets up the hash and comparison functions.
 * The initial reference count will be set to 1.
 *
 * @param hash A function pointer used to calculate the hash of keys.
 * @param compare A function pointer used to check equality between two keys.
 * @return A pointer to the newly created `Dictionary`, or NULL on failure.
 */
Dictionary*
init_dict(
    size_t (*hash)   (let_any key),
    int    (*compare)(let_any a, let_any b)
);

/**
 * @brief Retains the dictionary (increments reference count).
 *
 * Used to share ownership of the dictionary instance.
 *
 * @param self Pointer to the `Dictionary` instance.
 * @return The same pointer passed in `self`.
 */
Dictionary*
retain_dict(Dictionary* self);

/**
 * @brief Releases the dictionary (decrements reference count).
 *
 * If the reference count reaches zero, the memory associated with the
 * dictionary and its internal arrays is freed.
 *
 * @param self Pointer to the `Dictionary` instance to release.
 */
void
release_dict(Dictionary* self);

/**
 * @brief Retrieves a value from the dictionary.
 *
 * @param self Pointer to the `Dictionary` instance.
 * @param key The key to look up.
 * @return The value associated with the key. If the key is not found,
 * behavior depends on the implementation of `any` (likely returns NULL/Empty).
 */
any
dict_get(
    Dictionary* self,
    let_any key
);

/**
 * @brief Inserts or updates a value in the dictionary.
 *
 * If the key already exists, the value is updated. If not, a new entry is created.
 * This operation may trigger an incremental rehash step.
 *
 * @param self Pointer to the `Dictionary` instance.
 * @param key The key to associate the value with.
 * @param value The value to store.
 * @return `true` if the operation was successful, `false` otherwise (e.g., allocation failure).
 */
bool
dict_set(
    Dictionary* self,
    let_any key,
    let_any value
);

/**
 * @brief Removes a key-value pair from the dictionary.
 *
 * Marks the slot as DELETED.
 *
 * @param self Pointer to the `Dictionary` instance.
 * @param key The key to remove.
 * @return `true` if the key was found and removed, `false` if the key was not found.
 */
bool
dict_remove(
    Dictionary* self,
    let_any key
);

#endif //DICT_DICT_H
