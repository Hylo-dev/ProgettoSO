#ifndef C_VECTOR_H
#define C_VECTOR_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

/* =========================================================================
   CORE STRUCTURES
   ========================================================================= */

typedef struct {
    size_t ref_cnt; 
    size_t cap;     
    size_t len;     
} _VecHeader;

// Macro definizione tipo
#define Vector(T) struct { T *data; }

/* =========================================================================
   INTERNAL HELPER MACROS & FUNCS
   ========================================================================= */

#define _V_HDR(v) ((_VecHeader *)((char *)(v).data - sizeof(_VecHeader)))
#define _V_HAS_DATA(v) ((v).data != NULL)

#define vec_len(v) (_V_HAS_DATA(v) ? _V_HDR(v)->len : 0)
#define vec_cap(v) (_V_HAS_DATA(v) ? _V_HDR(v)->cap : 0)
#define vec_init(v) memset(&(v), 0, sizeof(v))

// --- Funzione Grow/Push Internal (Gestione COW) ---
static inline void*
_vec_grow_internal(
    void  *data,
    size_t elem_size,
    size_t needed_len
) {
    _VecHeader *hdr = data ? (_VecHeader *)((char *)data - sizeof(_VecHeader)) : NULL;
    
    if (hdr && hdr->ref_cnt > 1) {
        size_t new_cap = (needed_len > hdr->cap) ? (hdr->cap * 2) : hdr->cap;
        if (new_cap < 4) new_cap = 4;
        
        size_t total_size = sizeof(_VecHeader) + (new_cap * elem_size);
        _VecHeader *new_hdr = (_VecHeader *)malloc(total_size);
        
        new_hdr->ref_cnt = 1;
        new_hdr->len = hdr->len;
        new_hdr->cap = new_cap;
        memcpy((char*)new_hdr + sizeof(_VecHeader), data, hdr->len * elem_size);
        
        hdr->ref_cnt--; // Decrementa il vecchio
        return (char*)new_hdr + sizeof(_VecHeader);
    }

    if (hdr && needed_len > hdr->cap) {
        size_t new_cap = hdr->cap * 2;
        if (new_cap < 4) new_cap = 4;
        size_t total_size = sizeof(_VecHeader) + (new_cap * elem_size);
        hdr = (_VecHeader *)realloc(hdr, total_size);
        hdr->cap = new_cap;
        return (char*)hdr + sizeof(_VecHeader);
    }

    if (!hdr) {
        size_t new_cap = (needed_len > 4) ? needed_len : 4;
        size_t total_size = sizeof(_VecHeader) + (new_cap * elem_size);
        hdr = (_VecHeader *)malloc(total_size);
        hdr->ref_cnt = 1;
        hdr->cap = new_cap;
        hdr->len = 0;
        return (char*)hdr + sizeof(_VecHeader);
    }

    return data;
}

// Importa un array grezzo creando un nuovo Vector con Header valido
static inline void*
_vec_from_internal(
    const void  *src,
          size_t count,
          size_t elem_size
) {
    if (!src || count == 0) return NULL;

    // 1. Allocazione: Header + Dati
    size_t total_size = sizeof(_VecHeader) + (count * elem_size);
    _VecHeader *hdr = (_VecHeader *)malloc(total_size);
    
    // 2. Setup Header
    hdr->ref_cnt = 1;
    hdr->cap = count;  // La capacità iniziale è esattamente quanto serve
    hdr->len = count;  // La lunghezza è quella passata
    
    // 3. Copia dei dati grezzi nel buffer del vettore
    void *dest_data = (char *)hdr + sizeof(_VecHeader);
    memcpy(dest_data, src, count * elem_size);
    
    return dest_data;
}

static inline void
_vec_free_internal(
    void *data
) {
    if (!data) return;
    _VecHeader *hdr = (_VecHeader *)((char *)data - sizeof(_VecHeader));
    if (hdr->ref_cnt > 1) hdr->ref_cnt--;
    else free(hdr);
}

static inline void*
_vec_share_internal(
    void *data
) {
    if (!data) return NULL;
    _VecHeader *hdr = (_VecHeader *)((char *)data - sizeof(_VecHeader));
    hdr->ref_cnt++;
    return data;
}

// --- NUOVA FUNZIONE SHRINK ---
static inline void *
_vec_shrink_internal(
    void  *data,
    size_t elem_size
) {
    if (!data) return NULL;
    _VecHeader *hdr = (_VecHeader *)((char *)data - sizeof(_VecHeader));

    // Se è già ottimizzato, esci subito
    if (hdr->len == hdr->cap) return data;

    // Se è vuoto, libera tutto e torna NULL
    if (hdr->len == 0) {
        _vec_free_internal(data);
        return NULL;
    }

    size_t new_cap = hdr->len;
    size_t total_size = sizeof(_VecHeader) + (new_cap * elem_size);

    // CASO A: Condiviso (COW) -> Crea copia piccola e stacca
    if (hdr->ref_cnt > 1) {
        _VecHeader *new_hdr = (_VecHeader *)malloc(total_size);
        new_hdr->ref_cnt = 1;
        new_hdr->len = hdr->len;
        new_hdr->cap = new_cap;
        memcpy((char*)new_hdr + sizeof(_VecHeader), data, hdr->len * elem_size);
        
        hdr->ref_cnt--; 
        return (char*)new_hdr + sizeof(_VecHeader);
    }

    // CASO B: Esclusivo -> Realloc "in discesa"
    _VecHeader *new_hdr = (_VecHeader *)realloc(hdr, total_size);
    // Nota: realloc in discesa può spostare il puntatore o meno, ma torna sempre valido
    new_hdr->cap = new_cap;
    return (char*)new_hdr + sizeof(_VecHeader);
}

/* =========================================================================
   PUBLIC API
   ========================================================================= */

#define vec_free(v) do { _vec_free_internal((v).data); (v).data = NULL; } while(0)
#define vec_share(dest, src) do { (dest).data = _vec_share_internal((src).data); } while(0)

#define vec_push(v, val) do { \
    (v).data = _vec_grow_internal((v).data, sizeof(*(v).data), vec_len(v) + 1); \
    (v).data[_V_HDR(v)->len++] = (val); \
} while(0)

// SHRINK: Riassegna il puntatore dati al risultato dello shrink
#define vec_shrink(v) do { \
    (v).data = _vec_shrink_internal((v).data, sizeof(*(v).data)); \
} while(0)

#define vec_pop(v) ({ \
    typeof((v).data) _ret = NULL; \
    if (vec_len(v) > 0) { \
         (v).data = _vec_grow_internal((v).data, sizeof(*(v).data), vec_len(v)); \
         _ret = &(v).data[--_V_HDR(v)->len]; \
    } \
    _ret ? *_ret : (typeof(*(v).data)){0}; \
})

#define vec_remove_at(v, idx) do { \
    size_t _idx = (idx); \
    size_t _len = vec_len(v); \
    if (_idx < _len) { \
        (v).data = _vec_grow_internal((v).data, sizeof(*(v).data), _len); \
        memmove(&(v).data[_idx], &(v).data[_idx + 1], (_len - _idx - 1) * sizeof(*(v).data)); \
        _V_HDR(v)->len--; \
    } \
} while(0)

#define vec_remove_fast(v, idx) do { \
    size_t _idx = (idx); \
    size_t _len = vec_len(v); \
    if (_idx < _len) { \
        (v).data = _vec_grow_internal((v).data, sizeof(*(v).data), _len); \
        if (_idx != _len - 1) (v).data[_idx] = (v).data[_len - 1]; \
        _V_HDR(v)->len--; \
    } \
} while(0)

#define vec_indexof(v, val) ({ \
    ssize_t _found_idx = -1; \
    size_t _len = vec_len(v); \
    for (size_t _i = 0; _i < _len; ++_i) { \
        if ((v).data[_i] == (val)) { _found_idx = (ssize_t)_i; break; } \
    } \
    _found_idx; \
})

#define vec_from(v, ptr, count) do { \
    (v).data = _vec_from_internal((ptr), (count), sizeof(*(v).data)); \
} while(0)


#define foreach(elem, v) \
    for (typeof((v).data) elem = (v).data; \
         _V_HAS_DATA(v) && elem < (v).data + _V_HDR(v)->len; \
         ++elem)

#endif // C_VECTOR_H
