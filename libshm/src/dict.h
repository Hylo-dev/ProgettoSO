#ifndef SHM_DICT_H
#define SHM_DICT_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <sys/types.h>
#include <sys/ipc.h> 
#include <sys/sem.h>

#define SHM_NULL ((uintptr_t)0)

typedef enum: uint8_t {
    NODE_EMPTY   = 0,
    NODE_USED    = 1,
    NODE_DELETED = 2
} NodeState;

typedef enum: uint8_t {
    DICT_ERR = 0,
    DICT_ADDED,
    DICT_UPDATED
} DictResult;

typedef struct {
    uintptr_t key;
    uintptr_t val;
} DictNode;

typedef struct {
    uintptr_t elements_off;
    size_t size;
    size_t count;
} array;

/**
 * @brief Root Structure (Pure SysV)
 */
typedef struct {
    // -- SYNC SYSV --
    int sem_id; // ID del set semaforico SysV
    
    // -- MEMORY MANAGEMENT --
    uintptr_t heap_top;
    size_t total_capacity;
    
    // -- DICT STATE --
    array  array[2];
    size_t rehash_index;
    bool   rehashing;
    
    uint32_t magic;
} shm_header;

typedef struct {
    void       *base_addr;
    shm_header *header;
    size_t    (*hash)   (uintptr_t, void*);
    int       (*compare)(uintptr_t, uintptr_t, void*);
} DictHandle;

// Prototipi
bool shm_dict_init(
    void   *shm_base,
    size_t  total_shm_size
);

void shm_dict_attach(
    DictHandle *handle,
    void       *shm_base,
    size_t    (*hash)(uintptr_t, void*),
    int       (*cmp) (uintptr_t, uintptr_t, void*)
);

uintptr_t shm_dict_get(
    DictHandle *h,
    uintptr_t   key
);

bool      shm_dict_set   (DictHandle *h, uintptr_t key, uintptr_t value);
bool      shm_dict_remove(DictHandle *h, uintptr_t key );
uintptr_t shm_alloc_bytes(DictHandle *h, size_t    size);
void*     shm_ptr        (DictHandle *h, uintptr_t off );

#endif // SHM_DICT_H
