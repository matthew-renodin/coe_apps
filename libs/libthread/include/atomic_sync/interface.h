/**
 * @file interface.h
 * @brief Interface wrappers for use by liblockwrapper.
 */
#pragma once

#include <atomic_sync/types.h>
#include <lockwrapper/lockvka.h>

/**
 * @brief Create a lock interface object using the given lock
 *
 * Note that the lock itself should not be freed while the interface
 * is in use
 * 
 * @param       mutex               Lock for initializing the interface
 * @return                          Initialized Lock interface object
 */
lock_interface_t make_lock_interface(mutex_t *mutex);
