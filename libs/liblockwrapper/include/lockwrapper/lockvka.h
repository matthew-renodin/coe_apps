/**
 *  @file lockvka.h
 * 
 *  @brief Locking Mechanism for VKA
 **/
#pragma once

#include <lockwrapper/wrappers.h>

/**
 * @brief Initialize a vka object from the current lockvka
 *
 * @param       out_vka             VKA to be initialized with locking functions and data
 * @param       lockvka             lockvka object to use in the initialization
 */
void lockvka_make_vka(vka_t *out_vka, lockvka_t *lockvka);

/**
 * @brief Create a lockvka object from an existing vka
 *
 * @param       lockvka             lockvka to be initialized
 * @param       parent_vka          vka object whose behavior and data are to be locked
 * @param       lock                lock to be used for locking the vka
 */
void lockvka_attach(lockvka_t *lockvka, vka_t parent_vka, lock_interface_t lock);

/**
 * @brief Modify an existing vka by surrounding it with a lockvka 
 *        AND replacing its function pointers with locking versions
 *
 * @param       lockvka             lockvka to be initialized
 * @param       parent_vka          vka object to be locked
 * @param       lock                lock to be used for locking the vka
 */
static inline void lockvka_replace(lockvka_t *lockvka, vka_t *inout_vka, lock_interface_t lock) {
    lockvka_attach(lockvka, *inout_vka, lock);
    lockvka_make_vka(inout_vka, lockvka);
}

/**
 * @brief lock the lockvka
 * 
 * This call should be rarely used (if ever), but in the case that you need to
 * directly access internal data, you should lock the data before accessing it
 */
static inline int lockvka_lock(lockvka_t * lockvka) {
    assert(lockvka != NULL && lockvka->lock.mutex_lock != NULL);
    return lockvka->lock.mutex_lock(lockvka->lock.data);
}

/**
 * @brief Unlock the lockvka
 * 
 * This call should be rarely used (if ever), but in the case that you need to 
 * directly access internal data, you should lock the data before accessing it
 */
static inline int lockvka_unlock(lockvka_t * lockvka) {
    assert(lockvka != NULL && lockvka->lock.mutex_unlock != NULL);
    return lockvka->lock.mutex_unlock(lockvka->lock.data);
}