#ifndef SHM_DICT_H
#define SHM_DICT_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <sys/types.h>
#include <sys/ipc.h> 
#include <sys/sem.h> // Header fondamentale per SysV

// Tipo per gli offset relativi
typedef uintptr_t shm_offset_t;
#define SHM_NULL ((shm_offset_t)0)

typedef enum : uint8_t {
    NODE_EMPTY = 0,
    NODE_USED = 1,
    NODE_DELETED = 2
} NodeState;

typedef enum : uint8_t {
    DICT_ERR = 0,
    DICT_ADDED,
    DICT_UPDATED
} DictResult;

struct ShmDictElement {
    shm_offset_t key;
    shm_offset_t data;
};

typedef struct {
    shm_offset_t elements_off;
    size_t size;
    size_t num_elements;
} ShmArray;

/**
 * @brief Root Structure (Pure SysV)
 */
typedef struct {
    // -- SINCRONIZZAZIONE SYSV --
    int sem_id;                // ID del set semaforico SysV
    
    // -- MEMORY MANAGEMENT --
    shm_offset_t heap_top;
    size_t total_capacity;
    
    // -- DICT STATE --
    ShmArray array[2];
    size_t rehash_index;
    bool is_rehashing;
    
    uint32_t magic; 
} ShmRoot;

typedef struct {
    void *base_addr;
    ShmRoot *root;
    size_t (*hash)(shm_offset_t, void*);
    int (*compare)(shm_offset_t, shm_offset_t, void*);
} DictHandle;

// Prototipi
bool shm_dict_init(void *shm_base, size_t total_shm_size);
void shm_dict_attach(DictHandle *handle, void *shm_base, 
                     size_t (*hash_fn)(shm_offset_t, void*),
                     int (*cmp_fn)(shm_offset_t, shm_offset_t, void*));

shm_offset_t shm_dict_get(DictHandle *h, shm_offset_t key);
bool shm_dict_set(DictHandle *h, shm_offset_t key, shm_offset_t value);
bool shm_dict_remove(DictHandle *h, shm_offset_t key);
shm_offset_t shm_alloc_bytes(DictHandle *h, size_t size);
void* shm_ptr(DictHandle *h, shm_offset_t off);

#endif // SHM_DICT_H
