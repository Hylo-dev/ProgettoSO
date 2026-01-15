/**
 * @file shm_dict.c
 * @brief Implementation of System V Shared Memory Dictionary (Pure SysV IPC).
 */

#include "dict.h"
#include <stddef.h>
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
#define GET_KEY_OFFSET(tagged) ((uintptr_t)((tagged) & PTR_MASK))
#define GET_STATE(tagged)      ((NodeState)((tagged) & STATE_MASK))
#define TAG_OFFSET(off, state) ((uintptr_t)((off) | (state)))

// --- HELPER DI INDIRIZZAMENTO ---

static inline void*
resolve(void *base, uintptr_t off) {
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

static uintptr_t
_shmalloc(shm_header *root, size_t size) {
    size_t current = root->heap_top;
    size_t padding = (ALIGNMENT - (current % ALIGNMENT)) % ALIGNMENT;
    size_t aligned_start = current + padding;
    size_t new_top = aligned_start + size;

    if (new_top > root->total_capacity) {
        return SHM_NULL;
    }

    root->heap_top = new_top;
    return (uintptr_t)aligned_start;
}

uintptr_t
shm_alloc_bytes(
    DictHandle *h,
    size_t      size
) {
    lock(h->header->sem_id);
    uintptr_t off = _shmalloc(h->header, size);
    unlock(h->header->sem_id);
    return off;
}

void*
shm_ptr(DictHandle *h, uintptr_t off) {
    return resolve(h->base_addr, off);
}

// --- CORE LOGIC ---

static inline void
_rehashing_array_init(
    DictHandle *h,
    size_t      size
) {

    shm_header *header    = h->header;
    uintptr_t   new_arr   = _shmalloc(header, size * sizeof(DictNode));
        
    if (new_arr != SHM_NULL) {
        memset(resolve(h->base_addr, new_arr), 0, size * sizeof(DictNode));
            
        header->array[1]     = (array){new_arr, size, 0};
        header->rehashing    = true;
        header->rehash_index = 0;
    }
}

static inline uintptr_t
_dict_search(DictHandle *h, array *array, uintptr_t key) {
    if (unlikely(array->size == 0 || array->elements_off == SHM_NULL))
        return SHM_NULL;
    
    DictNode *elements = (DictNode *)resolve(h->base_addr, array->elements_off);
    const size_t hash_value = h->hash(key, h->base_addr);
    size_t index = hash_value & (array->size - 1);
    size_t i = 0;
    do {
        // Quadratic probing limitato + Linear
        size_t next_index = (i < 32) ? (index + (i+1)*(i+1)) & (array->size - 1) 
                                     : (index + (i+1)) & (array->size - 1);
        
        uintptr_t raw_tagged = elements[index].key;
        NodeState state = GET_STATE(raw_tagged);
        if (unlikely(state == NODE_EMPTY)) return SHM_NULL;
        
        uintptr_t current_key = GET_KEY_OFFSET(raw_tagged);
        if (likely(state == NODE_USED && h->compare(key, current_key, h->base_addr) == 0)) {
            return elements[index].val;
        }
        index = next_index;
        i++;
    } while (i < array->size);
    return SHM_NULL;
}

static inline bool
_dict_change_state(
    DictHandle *h,
    array *array,
    uintptr_t key,
    NodeState new_state
) {
    if (unlikely(array->size == 0)) return false;
    DictNode *elements = (DictNode *)resolve(h->base_addr, array->elements_off);
    const size_t hash_value = h->hash(key, h->base_addr);
    size_t index = hash_value & (array->size - 1);
    size_t i = 0;
    do {
        size_t next_index = (i < 32) ?
                            (index + (i+1)*(i+1)) & (array->size - 1) :
                            (index + i + 1) & (array->size - 1);
        uintptr_t raw_tagged = elements[index].key;
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
    array *array,
    DictNode element
) {
    if (unlikely(array->elements_off == SHM_NULL)) return DICT_ERR;
    DictNode *elements = (DictNode *)resolve(h->base_addr, array->elements_off);
    ssize_t best_position = -1;
    const size_t hash_value = h->hash(element.key, h->base_addr);
    size_t index = hash_value & (array->size - 1);
    size_t i = 0;
    do {
        size_t next_index = (i < 32) ? (index + (i+1)*(i+1)) & (array->size - 1) : (index + i + 1) & (array->size - 1);
        uintptr_t raw_tagged = elements[index].key;
        NodeState state = GET_STATE(raw_tagged);
        if (state == NODE_EMPTY) {
            if (best_position == -1) best_position = index;
            elements[best_position].key = TAG_OFFSET(element.key, NODE_USED);
            elements[best_position].val = element.val;
            return DICT_ADDED;
        }
        if (state == NODE_USED && h->compare(element.key, GET_KEY_OFFSET(raw_tagged), h->base_addr) == 0) {
            elements[index].key = TAG_OFFSET(element.key, NODE_USED);
            elements[index].val = element.val;
            return DICT_UPDATED;
        }
        if (state == NODE_DELETED && best_position == -1) best_position = index;
        index = next_index;
        i++;
    } while (i < array->size);
    if (best_position != -1) {
        elements[best_position].key = TAG_OFFSET(element.key, NODE_USED);
        elements[best_position].val = element.val;
        return DICT_ADDED;
    }
    return DICT_ERR;
}

static inline void
_dict_insert_blind(DictHandle *h, array *array, DictNode element) {
    DictNode *elements = (DictNode *)resolve(h->base_addr, array->elements_off);
    size_t index = h->hash(element.key, h->base_addr) & (array->size - 1);
    size_t i = 0;
    do {
        size_t next_index = (i < 32) ? (index + (i+1)*(i+1)) & (array->size - 1) : (index + i + 1) & (array->size - 1);
        if (GET_STATE(elements[index].key) == NODE_EMPTY) {
            elements[index].key = TAG_OFFSET(element.key, NODE_USED);
            elements[index].val = element.val;
            array->count++;
            return;
        }
        index = next_index;
        i++;
    } while (i < array->size);
}

static inline void
_dict_shrink(DictHandle *h) {

    array a = h->header->array[0];
    
    const size_t current_size = a.size;
    const size_t threshold    = (size_t)((double)current_size * RANGE_SHRINK);

    if (unlikely(!h->header->rehashing &&
        current_size > MIN_SIZE &&
        a.count < threshold)
    ) {
        size_t new_size = current_size / 2;
        if (unlikely(new_size < MIN_SIZE)) new_size = MIN_SIZE;

        _rehashing_array_init(h, new_size);
    }
}


static inline void dict_rehash_table(DictHandle *h) {
    shm_header *header = h->header;
    size_t moved = 0;
    DictNode *old_elems = (DictNode *)resolve(h->base_addr, header->array[0].elements_off);
    
    while (header->rehash_index < header->array[0].size && moved < 100) {
        DictNode *el = &old_elems[header->rehash_index];
        if (GET_STATE(el->key) == NODE_USED) {
            uintptr_t clean = GET_KEY_OFFSET(el->key);
            _dict_insert_blind(h, &header->array[1], (DictNode){clean, el->val});
            el->key = TAG_OFFSET(clean, NODE_DELETED);
            header->array[0].count--;
            moved++;
        }
        header->rehash_index++;
    }
    if (header->array[0].count == 0) {
        header->array[0] = header->array[1];
        header->array[1] = (array){SHM_NULL, 0, 0};
        header->rehashing = false;
        header->rehash_index = 0;
    }
}

// --- FUNZIONI PUBBLICHE (SYSV ADAPTED) ---

bool shm_dict_init(void *shm_base, size_t total_shm_size) {
    if (!shm_base) return false;
    shm_header *root = (shm_header *)shm_base;

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
    
    size_t root_size = sizeof(shm_header);
    size_t heap_start = (root_size + 63) & ~63;
    root->heap_top = heap_start;
    root->total_capacity = total_shm_size;
    root->magic = DICT_MAGIC;

    // Allocazione primo array
    uintptr_t arr_off = _shmalloc(root, MIN_SIZE * sizeof(DictNode));
    if (arr_off == SHM_NULL) return false;
    memset(resolve(shm_base, arr_off), 0, MIN_SIZE * sizeof(DictNode));

    root->array[0] = (array){arr_off, MIN_SIZE, 0};
    root->array[1] = (array){SHM_NULL, 0, 0};
    root->rehashing = false;
    root->rehash_index = 0;

    return true;
}

void shm_dict_attach(
    DictHandle  *handle,
    void        *shm_base, 
    size_t     (*hash_fn)(uintptr_t, void*),
    int        (*cmp_fn) (uintptr_t, uintptr_t, void*)
 ) {
    handle->base_addr = shm_base;
    handle->header    = (shm_header*)shm_base;
    handle->hash      = hash_fn;
    handle->compare   = cmp_fn;
}

uintptr_t shm_dict_get(DictHandle *h, uintptr_t key) {
    if (unlikely(!h || !h->header)) return SHM_NULL;
    
    lock(h->header->sem_id);
    if (h->header->rehashing) dict_rehash_table(h);
    
    uintptr_t res = SHM_NULL;
    if (h->header->rehashing) res = _dict_search(h, &h->header->array[1], key);
    if (res == SHM_NULL)      res = _dict_search(h, &h->header->array[0], key);
    
    unlock(h->header->sem_id);
    return res;
}

bool shm_dict_set(
    DictHandle *h,
    uintptr_t   key,
    uintptr_t   value
) {
    if (unlikely(!h || !h->header)) return false;
    
    lock(h->header->sem_id);
    shm_header *header = h->header;

    if (header->rehashing) {
        dict_rehash_table(h);
        
    } else if (header->array[0].count >= (size_t)(header->array[0].size * RANGE_RESIZE)) {
        const size_t new_size = header->array[0].size * 2;
        uintptr_t new_arr     = _shmalloc(header, new_size * sizeof(DictNode));
        
        if (new_arr != SHM_NULL) {
            memset(resolve(h->base_addr, new_arr), 0, new_size * sizeof(DictNode));
            
            header->array[1]     = (array){new_arr, new_size, 0};
            header->rehashing    = true;
            header->rehash_index = 0;
        }
    }

    bool success = false;
    DictNode el  = {key, value};

    if (header->rehashing && header->array[1].elements_off != SHM_NULL) {
        DictResult res = _dict_insert(h, &header->array[1], el);
        
        if (res != DICT_ERR) {
            if (res == DICT_ADDED) header->array[1].count++;
            _dict_change_state(h, &header->array[0], key, NODE_DELETED);
            success = true;
        }
        
    } else {
        DictResult res = _dict_insert(h, &header->array[0], el);
        if (res != DICT_ERR) {
            if (res == DICT_ADDED) header->array[0].count++;
            success = true;
        }
    }
    
    unlock(h->header->sem_id);
    return success;
}

bool shm_dict_remove(DictHandle *h, uintptr_t key) {
    if (unlikely(!h || !h->header)) return false;
    lock(h->header->sem_id);

    bool ret = false;
    if (h->header->rehashing) {
        dict_rehash_table(h);
        
        if (_dict_change_state(h, &h->header->array[1], key, NODE_DELETED)) {
            h->header->array[1].count--;
            ret = true;      
        }
    }
    
    if (!ret && _dict_change_state(h, &h->header->array[0], key, NODE_DELETED)) {
        h->header->array[0].count--;
        ret = true;
    }

    if (!ret) {
        unlock(h->header->sem_id);
        return false;
    }
    
    _dict_shrink(h);
    unlock(h->header->sem_id);
    return true;
}
