#pragma once

#include <lockwrapper/wrappers.h>

void lockvka_make_vka(vka_t *out_vka, lockvka_t *lockvka);

void lockvka_attach(lockvka_t *lockvka, vka_t parent_vka, lock_interface_t lock);

static inline void lockvka_replace(lockvka_t *lockvka, vka_t *inout_vka, lock_interface_t lock) {
    lockvka_attach(lockvka, *inout_vka, lock);
    lockvka_make_vka(inout_vka, lockvka);
}

/**
 * @brief lock the lockvka
 * 
 * This call should be rarely used, the locking mechanism is called without management for any calls into
 * the vspace object. But in the case that you need to directly access internal data, you should lock the
 * data before accessing it
 */
static inline int lockvka_lock(lockvka_t * lockvka) {
    assert(lockvka != NULL && lockvka->lock.mutex_lock != NULL);
    return lockvka->lock.mutex_lock(lockvka->lock.data);
}

/**
 * @brief Unlock the lockvka
 * 
 * This call should be rarely used, the locking mechanism is called without management for any calls into
 * the vspace object. But in the case that you need to directly access internal data, you should lock the
 * data before accessing it
 */
static inline int lockvka_unlock(lockvka_t * lockvka) {
    assert(lockvka != NULL && lockvka->lock.mutex_unlock != NULL);
    return lockvka->lock.mutex_unlock(lockvka->lock.data);
}