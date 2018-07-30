/**
 *  @file lockvspace.h
 * 
 *  @brief Locking Mechanism for VSpace Object
 **/
#pragma once

#include <lockwrapper/wrappers.h>

/**
 * @brief Initialize a vspace object from the current lockvspace
 *
 * @param       out_vspace              vspace to be initialized with locking functions and data
 * @param       lockvspace              lockvspace object to use in the initialization
 */
void lockvspace_make_vspace(vspace_t *out_vspace, lockvspace_t *lockvspace);

/**
 * @brief Create a lockvspace object from an existing vspace
 *
 * @param       lockvspace              lockvspace to be initialized
 * @param       parent_vspace           vspace object whose behavior and data are to be locked
 * @param       lock                    lock to be used for locking the vspace
 */
void lockvspace_attach(lockvspace_t *lockvspace, vspace_t parent_vspace, lock_interface_t lock);

/**
 * @brief Modify an existing vspace by surrounding it with a lockvspace 
 *        AND replacing its function pointers with locking versions
 *
 * @param       lockvspace              lockvspace to be initialized
 * @param       parent_vspace           vspace object to be locked
 * @param       lock                    lock to be used for locking the vspace
 */
static inline void lockvspace_replace(lockvspace_t *lockvspace, vspace_t *inout_vspace, lock_interface_t lock) {
    lockvspace_attach(lockvspace, *inout_vspace, lock);
    lockvspace_make_vspace(inout_vspace, lockvspace);
}

/**
 * @brief set the allocated_object_cookie for the inner vspace
 * 
 * This call should be rarely used, as the cookie is usually set at initialization of inner vspace 
 * before a lockvspace is attached, but it is EXTREMELY important that this call is used if you want
 * to set the allocated object cookie after a lockvspace is used
 */
void lockvspace_set_allocated_object_cookie(lockvspace_t *lockvspace, void *new_allocated_object_cookie);

/**
 * @brief lock the lockvspace
 * 
 * This call should be rarely used, the locking mechanism is called without management for any calls into
 * the vspace object. But in the case that you need to directly access internal data (as is the case when
 * you call into "sel4utils" functions), you should lock the data before accessing it
 * 
 * It would also be wise to only use this function if you have initialized lock to be re-entrant OR if you refer to
 * the parent vspace while holding the lock
 */
static inline int lockvspace_lock(vspace_t *vspace, lockvspace_t *lockvspace) {
    assert(vspace != NULL && vspace->sync_data == lockvspace && lockvspace != NULL && lockvspace->lock.mutex_lock != NULL);
    return lockvspace->lock.mutex_lock(lockvspace->lock.data);
}

/**
 * @brief Unlock the lockvspace
 * 
 * This call should be rarely used, the locking mechanism is called without management for any calls into
 * the vspace object. But in the case that you need to directly access internal data (as is the case when
 * you call into "sel4utils" functions), you should lock the data before accessing it
 * 
 * It would also be wise to only use this function if you have initialized lock to be re-entrant OR if you refer to
 * the parent vspace while holding the lock
 */
static inline int lockvspace_unlock(vspace_t *vspace, lockvspace_t * lockvspace) {
    assert(vspace != NULL && vspace->sync_data == lockvspace && lockvspace != NULL && lockvspace->lock.mutex_lock != NULL);
    return lockvspace->lock.mutex_unlock(lockvspace->lock.data);
}