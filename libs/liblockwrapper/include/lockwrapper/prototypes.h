#pragma once

#include <sel4/sel4.h>
#include <vka/vka.h>

#include <stdbool.h>

typedef struct lock_interface lock_interface_t;

struct lock_interface {
	void *data;
	int (*mutex_lock)(void *m);
	int (*mutex_unlock)(void *m);
};

typedef struct lockvka{
    vka_t parent_vka;
    lock_interface_t lock;
} lockvka_t;

static inline vka_t *
inner_vka(void * data) {
    assert(data);
    return &(((lockvka_t *) (data))->parent_vka);
}

static inline lock_interface_t *
inner_lock(void * data) {
    assert(data);
    return &(((lockvka_t *) (data))->lock);
}

/* This horrendous section of code prevents duplication of lots of code */
#define LOCKVKA_CALL_OPS_RETURN(type, lock, alloc, operation,...) do { \
    type error; \
    assert(lock != NULL && alloc != NULL && alloc->operation != NULL); \
    lock->mutex_lock(lock->data); \
    error = alloc->operation(alloc->data, __VA_ARGS__);\
    lock->mutex_unlock(lock->data);\
    return error; \
} while(0)
#define LOCKVKA_CALL_RETURN(type, data, ...) LOCKVKA_CALL_OPS_RETURN(type, inner_lock(data), inner_vka(data), __VA_ARGS__)

#define LOCKVKA_CALL_OPS_VOID(lock, alloc, operation,...) do { \
    assert(lock != NULL && alloc != NULL && alloc->operation != NULL); \
    lock->mutex_lock(lock->data); \
    alloc->operation(alloc->data, __VA_ARGS__);\
    lock->mutex_unlock(lock->data);\
    return;\
} while(0)
#define LOCKVKA_CALL_VOID(data, ...) LOCKVKA_CALL_OPS_VOID(inner_lock(data), inner_vka(data), __VA_ARGS__)

static int lockvka_cspace_alloc(void *data, seL4_CPtr *res) {
    LOCKVKA_CALL_RETURN(int, data, cspace_alloc, res);
}

static void lockvka_cspace_make_path(void *data, seL4_CPtr slot, cspacepath_t *res) {
    LOCKVKA_CALL_VOID(data, cspace_make_path, slot, res);
}

static int lockvka_utspace_alloc(void *data, const cspacepath_t *dest, seL4_Word type, seL4_Word size_bits, seL4_Word *res) {
    LOCKVKA_CALL_RETURN(int, data, utspace_alloc, dest, type, size_bits, res);
}

static int lockvka_utspace_alloc_at(void *data, const cspacepath_t *dest, seL4_Word type, seL4_Word size_bits, uintptr_t paddr, seL4_Word *cookie) {
    LOCKVKA_CALL_RETURN(int, data, utspace_alloc_at, dest, type, size_bits, paddr, cookie);
}

static int lockvka_utspace_alloc_maybe_device(void *data, const cspacepath_t *dest, seL4_Word type, seL4_Word size_bits, bool can_use_dev, seL4_Word *res){
    LOCKVKA_CALL_RETURN(int, data, utspace_alloc_maybe_device, dest, type, size_bits, can_use_dev, res);
}

static void lockvka_utspace_free(void *data, seL4_Word type, seL4_Word size_bits, seL4_Word target) {
    LOCKVKA_CALL_VOID(data, utspace_free, type, size_bits, target);
}

static uintptr_t lockvka_utspace_paddr(void *data, seL4_Word target, seL4_Word type, seL4_Word size_bits) {
    LOCKVKA_CALL_RETURN(uintptr_t, data, utspace_paddr, target, type, size_bits);
}

static void lockvka_cspace_free(void *data, seL4_CPtr slot) {
    LOCKVKA_CALL_VOID(data, cspace_free, slot);
}

static inline void lockvka_make_vka(vka_t *out_vka, lockvka_t *lockvka) {
    assert(out_vka);
    assert(lockvka);

    out_vka->data = (void *) lockvka;
    out_vka->cspace_alloc = &lockvka_cspace_alloc;
    out_vka->cspace_make_path = &lockvka_cspace_make_path;
    out_vka->utspace_alloc = &lockvka_utspace_alloc;
    out_vka->utspace_alloc_maybe_device = &lockvka_utspace_alloc_maybe_device;
    out_vka->utspace_alloc_at = &lockvka_utspace_alloc_at;
    out_vka->cspace_free = &lockvka_cspace_free;
    out_vka->utspace_free = &lockvka_utspace_free;
    out_vka->utspace_paddr = &lockvka_utspace_paddr;
}

static inline void lockvka_attach(lockvka_t *lockvka, vka_t parent_vka, lock_interface_t lock) {
    lockvka->parent_vka = parent_vka;
    lockvka->lock = lock;
}

static inline void lockvka_replace(lockvka_t *lockvka, vka_t *inout_vka, lock_interface_t lock) {
    lockvka_attach(lockvka, *inout_vka, lock);
    lockvka_make_vka(inout_vka, lockvka);
}