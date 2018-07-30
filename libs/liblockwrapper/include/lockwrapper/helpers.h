/**
 *  @file helpers.h
 * 
 *  @brief This file contains common macro functions used to surround
 *  the liblockwrapper functions.
 * 
 *  Overall, the macros are a little ugly, but prevent hundreds of lines
 *  of duplicated code to surround all vspace and vka functions with locks 
 **/
#pragma once

#include <lockwrapper/types.h>

#define ERROR_CHECK(lock, object, operation) assert(lock != NULL && object != NULL && object->operation != NULL)
#define LOCKED(lock, operation, action) do { \
    lock->mutex_lock(lock->data); \
    action;\
    lock->mutex_unlock(lock->data); \
} while(0)

#define LOCKWRAPPER_CALL_OPS_RETURN(type, lock, object, operation,...) do { \
    type error; \
    ERROR_CHECK(lock, object, operation); \
    LOCKED(lock, operation, error = object->operation(__VA_ARGS__));\
    return error; \
} while(0)

#define LOCKWRAPPER_CALL_OPS_VOID(lock, object, operation,...) do { \
    ERROR_CHECK(lock, object, operation); \
    LOCKED(lock, operation, object->operation(__VA_ARGS__));\
    return;\
} while(0)


/******************************************************************************
 * VKA Helpers
 *****************************************************************************/
static inline lock_interface_t *
lockvka_inner_lock(void * data) {
    assert(data);
    lockvka_t *lockvka = (lockvka_t *) data;
    return &(lockvka->lock);
}

static inline vka_t *
lockvka_inner_vka(void * data) {
    assert(data);
    lockvka_t *lockvka = (lockvka_t *) data;
    return &(lockvka->parent_vka);
}

#define LOCKVKA_CALL_RETURN(type, data, operation, ...) LOCKWRAPPER_CALL_OPS_RETURN(type, lockvka_inner_lock(data), lockvka_inner_vka(data), operation, lockvka_inner_vka(data)->data, __VA_ARGS__)
#define LOCKVKA_CALL_VOID(data, operation, ...) LOCKWRAPPER_CALL_OPS_VOID(lockvka_inner_lock(data), lockvka_inner_vka(data), operation, lockvka_inner_vka(data)->data, __VA_ARGS__)


/******************************************************************************
 * VSPACE Helpers
 *****************************************************************************/
static inline lockvspace_t *
lockvspace_from_vspace(vspace_t* vspace) {
    assert(vspace->sync_data);
    return (lockvspace_t *) (vspace->sync_data);
}

static inline lock_interface_t *
lockvspace_inner_lock(vspace_t* vspace) {
    assert(vspace);
    lockvspace_t *lockvspace = lockvspace_from_vspace(vspace);
    return &(lockvspace->lock);
}

static inline vspace_t *
lockvspace_inner_vspace(vspace_t* vspace) {
    assert(vspace);
    lockvspace_t *lockvspace = lockvspace_from_vspace(vspace);
    return &(lockvspace->parent_vspace);
}

//#define _LOCKVSPACE_IDENTIFY
#ifdef _LOCKVSPACE_IDENTIFY
#define LOCKVSPACE_IDENTIFY(data, operation, x) do { \
    ZF_LOGE("Called lockvspace_%s %p, with VSpace object %p, Lock %p, Inner Vspace %p", #operation, lockvspace_inner_vspace(data)->operation, data, lockvspace_inner_lock(data), lockvspace_inner_vspace(data));\
    x; \
} while(0)
#else
#define LOCKVSPACE_IDENTIFY(data, operation, x) x
#endif

#define LOCKVSPACE_CALL_RETURN(type, data, operation, ...) LOCKVSPACE_IDENTIFY(data, operation, LOCKWRAPPER_CALL_OPS_RETURN(type, lockvspace_inner_lock(data), lockvspace_inner_vspace(data), operation, lockvspace_inner_vspace(data), __VA_ARGS__))
#define LOCKVSPACE_CALL_VOID(data, operation, ...) LOCKVSPACE_IDENTIFY(data, operation, LOCKWRAPPER_CALL_OPS_VOID(lockvspace_inner_lock(data), lockvspace_inner_vspace(data), operation, lockvspace_inner_vspace(data), __VA_ARGS__))

#define LOCKVSPACE_CALL_RETURN_NOARG(type, data, operation) LOCKVSPACE_IDENTIFY(data, operation, LOCKWRAPPER_CALL_OPS_RETURN(type, lockvspace_inner_lock(data), lockvspace_inner_vspace(data), operation, lockvspace_inner_vspace(data)))
#define LOCKVSPACE_CALL_VOID_NOARG(data, operation) LOCKVSPACE_IDENTIFY(data, operation, LOCKWRAPPER_CALL_OPS_VOID(lockvspace_inner_lock(data), lockvspace_inner_vspace(data), operation, lockvspace_inner_vspace(data)))