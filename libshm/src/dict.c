/**
 * @file shm_dict.c
 * @brief Implementation of System V Shared Memory Dictionary (Pure SysV IPC).
 */

#include "dict.h"
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>

#ifndef likely
#define likely(x) __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)
#endif

#define MIN_SIZE 1024
#define RANGE_RESIZE 0.60
#define DICT_MAGIC 0xCAFEBABE
#define ALIGNMENT 8

#define STATE_MASK 0x7ULL
#define PTR_MASK   (~STATE_MASK)
#define GET_KEY_OFFSET(tagged) ((shm_offset_t)((tagged) & PTR_MASK))
#define GET_STATE(tagged)      ((NodeState)((tagged) & STATE_MASK))
#define TAG_OFFSET(off, state) ((shm_offset_t)((off) | (state)))

// --- HELPER DI INDIRIZZAMENTO ---

inline void*
resolve(void *base, shm_offset_t off) {
    if (off == SHM_NULL) return NULL;
    return (char*)base + off;
}

// --- HELPER SINCRONIZZAZIONE (PURE SYSV) ---

// Unione necessaria per semctl (standard SysV)
union _semun {
    int val;
    struct semid_ds *buf;
    unsigned short *array;
};

static inline void
lock(int semid) {
    // Operazione P (Wait/Lock): Decrementa il semaforo (0, -1)
    // SEM_UNDO: Se crashiamo, il kernel annulla questa operazione (sbloccando)
    struct sembuf op = {0, -1, SEM_UNDO};
    
    // Loop per gestire segnali (EINTR)
    while (semop(semid, &op, 1) == -1) {
        if (errno != EINTR) {
            perror("shm_dict: semop lock failed");
            break;
        }
    }
}

static inline void
unlock(int semid) {
    // Operazione V (Signal/Unlock): Incrementa il semaforo (0, +1)
    struct sembuf op = {0, 1, SEM_UNDO};
    
    while (semop(semid, &op, 1) == -1) {
        if (errno != EINTR) {
            perror("shm_dict: semop unlock failed");
            break;
        }
    }
}

// --- ALLOCATORE ---

static shm_offset_t
_shm_alloc_internal(ShmRoot *root, size_t size) {
    size_t current = root->heap_top;
    size_t padding = (ALIGNMENT - (current % ALIGNMENT)) % ALIGNMENT;
    size_t aligned_start = current + padding;
    size_t new_top = aligned_start + size;

    if (new_top > root->total_capacity) {
        return SHM_NULL;
    }

    root->heap_top = new_top;
    return (shm_offset_t)aligned_start;
}

shm_offset_t
shm_alloc_bytes(DictHandle *h, size_t size) {
    lock(h->root->sem_id);
    shm_offset_t off = _shm_alloc_internal(h->root, size);
    unlock(h->root->sem_id);
    return off;
}

void*
shm_ptr(DictHandle *h, shm_offset_t off) {
    return resolve(h->base_addr, off);
}

// --- CORE LOGIC ---

static inline shm_offset_t
_dict_search(DictHandle *h, ShmArray *array, shm_offset_t key) {
    if (unlikely(array->size == 0 || array->elements_off == SHM_NULL))
        return SHM_NULL;
    
    struct ShmDictElement *elements = (struct ShmDictElement *)resolve(h->base_addr, array->elements_off);
    const size_t hash_value = h->hash(key, h->base_addr);
    size_t index = hash_value & (array->size - 1);
    size_t i = 0;
    do {
        // Quadratic probing limitato + Linear
        size_t next_index = (i < 32) ? (index + (i+1)*(i+1)) & (array->size - 1) 
                                     : (index + (i+1)) & (array->size - 1);
        
        shm_offset_t raw_tagged = elements[index].key;
        NodeState state = GET_STATE(raw_tagged);
        if (unlikely(state == NODE_EMPTY)) return SHM_NULL;
        
        shm_offset_t current_key = GET_KEY_OFFSET(raw_tagged);
        if (likely(state == NODE_USED && h->compare(key, current_key, h->base_addr) == 0)) {
            return elements[index].data;
        }
        index = next_index;
        i++;
    } while (i < array->size);
    return SHM_NULL;
}

static inline bool
_dict_change_state(
    DictHandle *h,
    ShmArray *array,
    shm_offset_t key,
    NodeState new_state
) {
    if (unlikely(array->size == 0)) return false;
    struct ShmDictElement *elements = (struct ShmDictElement *)resolve(h->base_addr, array->elements_off);
    const size_t hash_value = h->hash(key, h->base_addr);
    size_t index = hash_value & (array->size - 1);
    size_t i = 0;
    do {
        size_t next_index = (i < 32) ?
                            (index + (i+1)*(i+1)) & (array->size - 1) :
                            (index + i + 1) & (array->size - 1);
        shm_offset_t raw_tagged = elements[index].key;
        NodeState state = GET_STATE(raw_tagged);
        if (state == NODE_EMPTY) return false;
        if (state == NODE_USED && h->compare(key, GET_KEY_OFFSET(raw_tagged), h->base_addr) == 0) {
            elements[index].key = TAG_OFFSET(GET_KEY_OFFSET(raw_tagged), new_state);
            return true;
        }
        index = next_index;
        i++;
    } while (i < array->size);
    return false;
}

static inline DictResult
_dict_insert(
    DictHandle *h,
    ShmArray *array,
    struct ShmDictElement element
) {
    if (unlikely(array->elements_off == SHM_NULL)) return DICT_ERR;
    struct ShmDictElement *elements = (struct ShmDictElement *)resolve(h->base_addr, array->elements_off);
    ssize_t best_position = -1;
    const size_t hash_value = h->hash(element.key, h->base_addr);
    size_t index = hash_value & (array->size - 1);
    size_t i = 0;
    do {
        size_t next_index = (i < 32) ? (index + (i+1)*(i+1)) & (array->size - 1) : (index + i + 1) & (array->size - 1);
        shm_offset_t raw_tagged = elements[index].key;
        NodeState state = GET_STATE(raw_tagged);
        if (state == NODE_EMPTY) {
            if (best_position == -1) best_position = index;
            elements[best_position].key = TAG_OFFSET(element.key, NODE_USED);
            elements[best_position].data = element.data;
            return DICT_ADDED;
        }
        if (state == NODE_USED && h->compare(element.key, GET_KEY_OFFSET(raw_tagged), h->base_addr) == 0) {
            elements[index].key = TAG_OFFSET(element.key, NODE_USED);
            elements[index].data = element.data;
            return DICT_UPDATED;
        }
        if (state == NODE_DELETED && best_position == -1) best_position = index;
        index = next_index;
        i++;
    } while (i < array->size);
    if (best_position != -1) {
        elements[best_position].key = TAG_OFFSET(element.key, NODE_USED);
        elements[best_position].data = element.data;
        return DICT_ADDED;
    }
    return DICT_ERR;
}

static inline void
_dict_insert_blind(DictHandle *h, ShmArray *array, struct ShmDictElement element) {
    struct ShmDictElement *elements = (struct ShmDictElement *)resolve(h->base_addr, array->elements_off);
    size_t index = h->hash(element.key, h->base_addr) & (array->size - 1);
    size_t i = 0;
    do {
        size_t next_index = (i < 32) ? (index + (i+1)*(i+1)) & (array->size - 1) : (index + i + 1) & (array->size - 1);
        if (GET_STATE(elements[index].key) == NODE_EMPTY) {
            elements[index].key = TAG_OFFSET(element.key, NODE_USED);
            elements[index].data = element.data;
            array->num_elements++;
            return;
        }
        index = next_index;
        i++;
    } while (i < array->size);
}

static inline void dict_rehash_table(DictHandle *h) {
    ShmRoot *root = h->root;
    size_t moved = 0;
    struct ShmDictElement *old_elems = (struct ShmDictElement *)resolve(h->base_addr, root->array[0].elements_off);
    
    while (root->rehash_index < root->array[0].size && moved < 100) {
        struct ShmDictElement *el = &old_elems[root->rehash_index];
        if (GET_STATE(el->key) == NODE_USED) {
            shm_offset_t clean = GET_KEY_OFFSET(el->key);
            _dict_insert_blind(h, &root->array[1], (struct ShmDictElement){clean, el->data});
            el->key = TAG_OFFSET(clean, NODE_DELETED);
            root->array[0].num_elements--;
            moved++;
        }
        root->rehash_index++;
    }
    if (root->array[0].num_elements == 0) {
        root->array[0] = root->array[1];
        root->array[1] = (ShmArray){SHM_NULL, 0, 0};
        root->is_rehashing = false;
        root->rehash_index = 0;
    }
}

// --- FUNZIONI PUBBLICHE (SYSV ADAPTED) ---

bool shm_dict_init(void *shm_base, size_t total_shm_size) {
    if (!shm_base) return false;
    ShmRoot *root = (ShmRoot *)shm_base;

    // CREAZIONE SEMAFORO SYSV
    // Usiamo IPC_PRIVATE perché salviamo l'ID nella memoria condivisa accessibile a tutti
    int semid = semget(IPC_PRIVATE, 1, IPC_CREAT | 0666);
    if (semid == -1) {
        perror("semget failed");
        return false;
    }

    // INIZIALIZZAZIONE SEMAFORO A 1 (MUTEX)
    union _semun arg;
    arg.val = 1;
    if (semctl(semid, 0, SETVAL, arg) == -1) {
        perror("semctl init failed");
        return false;
    }

    // Setup Root
    root->sem_id = semid; // Salviamo l'ID così gli altri processi lo trovano
    
    size_t root_size = sizeof(ShmRoot);
    size_t heap_start = (root_size + 63) & ~63;
    root->heap_top = heap_start;
    root->total_capacity = total_shm_size;
    root->magic = DICT_MAGIC;

    // Allocazione primo array
    shm_offset_t arr_off = _shm_alloc_internal(root, MIN_SIZE * sizeof(struct ShmDictElement));
    if (arr_off == SHM_NULL) return false;
    memset(resolve(shm_base, arr_off), 0, MIN_SIZE * sizeof(struct ShmDictElement));

    root->array[0] = (ShmArray){arr_off, MIN_SIZE, 0};
    root->array[1] = (ShmArray){SHM_NULL, 0, 0};
    root->is_rehashing = false;
    root->rehash_index = 0;

    return true;
}

void shm_dict_attach(DictHandle *handle, void *shm_base, 
                     size_t (*hash_fn)(shm_offset_t, void*),
                     int (*cmp_fn)(shm_offset_t, shm_offset_t, void*)) {
    handle->base_addr = shm_base;
    handle->root = (ShmRoot *)shm_base;
    handle->hash = hash_fn;
    handle->compare = cmp_fn;
    // Il sem_id viene letto automaticamente da handle->root->sem_id
}

shm_offset_t shm_dict_get(DictHandle *h, shm_offset_t key) {
    if (!h || !h->root) return SHM_NULL;
    
    lock(h->root->sem_id);
    
    if (h->root->is_rehashing) dict_rehash_table(h);
    
    shm_offset_t res = SHM_NULL;
    if (h->root->is_rehashing) res = _dict_search(h, &h->root->array[1], key);
    if (res == SHM_NULL) res = _dict_search(h, &h->root->array[0], key);
    
    unlock(h->root->sem_id);
    return res;
}

bool shm_dict_set(DictHandle *h, shm_offset_t key, shm_offset_t value) {
    if (!h || !h->root) return false;
    
    lock(h->root->sem_id);
    ShmRoot *root = h->root;

    if (root->is_rehashing) {
        dict_rehash_table(h);
    } else if (root->array[0].num_elements >= (size_t)(root->array[0].size * RANGE_RESIZE)) {
        size_t new_size = root->array[0].size * 2;
        shm_offset_t new_arr = _shm_alloc_internal(root, new_size * sizeof(struct ShmDictElement));
        if (new_arr != SHM_NULL) {
            memset(resolve(h->base_addr, new_arr), 0, new_size * sizeof(struct ShmDictElement));
            root->array[1] = (ShmArray){new_arr, new_size, 0};
            root->is_rehashing = true;
            root->rehash_index = 0;
        }
    }

    bool success = false;
    struct ShmDictElement el = {key, value};
    
    if (root->is_rehashing && root->array[1].elements_off != SHM_NULL) {
        DictResult res = _dict_insert(h, &root->array[1], el);
        if (res != DICT_ERR) {
            if (res == DICT_ADDED) root->array[1].num_elements++;
            _dict_change_state(h, &root->array[0], key, NODE_DELETED);
            success = true;
        }
    } else {
        DictResult res = _dict_insert(h, &root->array[0], el);
        if (res != DICT_ERR) {
            if (res == DICT_ADDED) root->array[0].num_elements++;
            success = true;
        }
    }
    
    unlock(h->root->sem_id);
    return success;
}

bool shm_dict_remove(DictHandle *h, shm_offset_t key) {
    if (!h || !h->root) return false;
    lock(h->root->sem_id);
    
    bool removed = false;
    if (h->root->is_rehashing) {
        dict_rehash_table(h);
        if (_dict_change_state(h, &h->root->array[1], key, NODE_DELETED)) {
            h->root->array[1].num_elements--;
            removed = true;
        }
    }
    if (!removed && _dict_change_state(h, &h->root->array[0], key, NODE_DELETED)) {
        h->root->array[0].num_elements--;
        removed = true;
    }
    
    unlock(h->root->sem_id);
    return removed;
}
