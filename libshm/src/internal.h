#ifndef INTERNAL_H
#define INTERNAL_H

#include <pthread.h>
#include <stdatomic.h>
#include "libshm.h"

#define MAX_SEGMENTS 256  // Aumentiamo per supportare espansioni massicce
#define MIN_DICT_SIZE 64  // Dimensione minima buckets

// --- TIPI DI BASE ---

// Il puntatore universale (Segmento + Offset)
typedef struct {
    uint16_t seg_idx; 
    uint32_t offset;  
} shmptr_t;

// Valore nullo per shmptr_t
#define SHM_NULL ((shmptr_t){0, 0})

// --- GLI ELEMENTI (BUCKETS) ---

// Questa struttura forma l'array gigante nei Data Segments.
// NON contiene "next" (usiamo Open Addressing quadratico come nel tuo dict.c).
typedef struct {
    char key[64];            // Chiave fissa
    shmptr_t value;          // Puntatore al dato vero (blob)
    
    // Lock granulare per QUESTA cella. 
    // Protegge letture/scritture concorrenti sulla stessa chiave.
    pthread_rwlock_t lock;   
    
    uint32_t state;          // EMPTY, USED, TOMBSTONE
    uint32_t hash_cache;     // Salviamo l'hash per velocizzare il rehash
} shm_entry_t;


// --- LE TABELLE HASH (STORAGE) ---

// Rappresenta una delle due tabelle (ht[0] o ht[1])
typedef struct {
    shmptr_t table_ptr;      // Puntatore all'inizio dell'array shm_entry_t[]
    uint64_t size;           // Dimensione totale (potenza di 2)
    uint64_t sizemask;       // size - 1 (per calcolo indice veloce)
    uint64_t used;           // Numero di elementi occupati
} shm_dictht_t;


// --- IL CONTROLLER (NEL MASTER BLOCK) ---

// Questa è la struttura che sta fissa all'inizio della memoria.
typedef struct {
    // Le due tabelle:
    // ht[0]: Tabella primaria
    // ht[1]: Tabella nuova (durante il rehash)
    shm_dictht_t ht[2];
    
    // Indice di rehash incrementale.
    // -1 se non stiamo rehashando.
    // >= 0 indica l'indice del bucket che stiamo spostando.
    atomic_long rehashidx; 
    
    // Lock GLOBALE per operazioni strutturali (es. inizio resize).
    // Nota: Le get/put normali NON prendono questo lock, usano i lock delle entry.
    // Questo serve solo per coordinare l'allocazione della nuova tabella.
    pthread_mutex_t control_lock;
    
} shm_dict_t;


// --- IL MASTER BLOCK ---

typedef struct {
    uint32_t magic;
    
    // Gestione Segmenti
    int shm_ids[MAX_SEGMENTS];
    size_t shm_sizes[MAX_SEGMENTS];
    size_t shm_used[MAX_SEGMENTS]; // Bump pointer offset
    uint32_t segment_count;
    pthread_mutex_t alloc_lock;    // Protegge l'allocazione di nuovi segmenti
    
    // Il Dizionario (Unico oggetto fisso)
    shm_dict_t dict;
    
} shm_master_t;

#endif
