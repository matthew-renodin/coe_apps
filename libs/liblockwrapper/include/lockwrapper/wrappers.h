/**
 *  @file wrappers.h
 * 
 *  @brief Initialize lock interfaces from libseL4sync locks
 **/
#pragma once

#include <lockwrapper/types.h>
#include <sync/mutex.h>
#include <sync/recursive_mutex.h>

/**
 * @brief Initialize a lock interface object from a seL4 sync mutex
 *
 * @param       m                       pointer to sync mutex (DO NOT FREE MUTEX)
 * @return      lock_interface_t        interface implementing lock_interface_t
 */
lock_interface_t sync_mutex_make_interface(sync_mutex_t * m);

/**
 * @brief Initialize a lock interface object from a seL4 sync recursive mutex
 *
 * @param       m                       pointer to sync mutex (DO NOT FREE MUTEX)
 * @return      lock_interface_t        interface implementing lock_interface_t
 */
lock_interface_t sync_recursive_mutex_make_interface(sync_recursive_mutex_t * m);