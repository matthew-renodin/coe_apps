#pragma once

#include <lockwrapper/types.h>
#include <sync/mutex.h>
#include <sync/recursive_mutex.h>

static int sync_mutex_lock_generic(void * m) {
    return sync_mutex_lock((sync_mutex_t *) m);
}

static int sync_mutex_unlock_generic(void * m) {
    return sync_mutex_unlock((sync_mutex_t *) m);
}

static inline lock_interface_t
sync_mutex_make_interface(sync_mutex_t * m) {
    lock_interface_t interface;
    interface.data = (void *) m;
    interface.mutex_lock = &sync_mutex_lock_generic;
    interface.mutex_unlock = &sync_mutex_unlock_generic;
    return interface;
}

static int sync_recursive_mutex_lock_generic(void * m) {
    return sync_recursive_mutex_lock((sync_recursive_mutex_t *) m);
}

static int sync_recursive_mutex_unlock_generic(void * m) {
    return sync_recursive_mutex_unlock((sync_recursive_mutex_t *) m);
}

static inline lock_interface_t
sync_recursive_mutex_make_interface(sync_recursive_mutex_t * m) {
    lock_interface_t interface;
    interface.data = (void *) m;
    interface.mutex_lock = &sync_recursive_mutex_lock_generic;
    interface.mutex_unlock = &sync_recursive_mutex_unlock_generic;
    return interface;
}
