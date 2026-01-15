/**
 * @file libshm.h
 * @brief Simple Shared Memory Key-Value Store (Zero-Copy)
 * @project libshm
 */

#ifndef LIBSHM_H
#define LIBSHM_H

#include <stddef.h>
#include <stdint.h>
#include <errno.h> 

// Modalità di accesso per acquire
typedef enum {
    SHM_READ  = 0,
    SHM_WRITE = 1
} shm_mode_t;

// --------------------------------------------------------------------------
// SETUP
// --------------------------------------------------------------------------

/**
 * @brief Inizializza/Connette la libshm.
 * * @param app_name Nome univoco dell'applicazione (per la chiave IPC).
 * @param size Dimensione iniziale (suggerita 1MB+).
 * @return 0 su successo, -1 su errore.
 * * @error EACCES Permessi insufficienti per shmget/shmat.
 * @error ENOMEM Impossibile allocare la memoria richiesta.
 * @error EINVAL Parametri non validi.
 */
int shm_init(const char *app_name, size_t size);

/**
 * @brief Si stacca dalla shared memory (detach).
 * @return 0 su successo, -1 su errore.
 */
int shm_detach(void);

/**
 * @brief Distrugge fisicamente i segmenti (shmctl IPC_RMID).
 * Da usare solo allo shutdown del master.
 */
int shm_destroy(const char *app_name);

// --------------------------------------------------------------------------
// DIZIONARIO & ALLOCAZIONE
// --------------------------------------------------------------------------

/**
 * @brief Crea una nuova voce e alloca spazio.
 * * @return 0 su successo, -1 su errore.
 * * @error EEXIST La chiave esiste già.
 * @error ENOMEM Spazio esaurito nei segmenti (anche dopo resize).
 * @error ENAMETOOLONG Chiave troppo lunga.
 * @error EOWNERDEAD Rilevato mutex corrotto (cleanup necessario).
 */
int shm_put(const char *key, size_t size, const void *data);

/**
 * @brief Rimuove una voce (Soft Delete).
 * * @error ENOENT Chiave non trovata.
 */
int shm_remove(const char *key);

// --------------------------------------------------------------------------
// CORE: ACQUIRE / RELEASE
// --------------------------------------------------------------------------

/**
 * @brief Ottiene puntatore diretto ai dati (blocca il thread).
 * * @return Puntatore ai dati o NULL su errore.
 * * @error ENOENT Chiave non trovata.
 * @error EDEADLK Deadlock rilevato.
 * @error EOWNERDEAD Il processo precedente è crashato mentre aveva il lock.
 * (La lib proverà a recuperare, ma controlla i dati!).
 */
void *shm_acquire(const char *key, shm_mode_t mode);

/**
 * @brief Rilascia il lock.
 * * @error EINVAL Chiave non valida o lock non posseduto.
 */
int shm_release(const char *key);

#endif // LIBSHM_H
