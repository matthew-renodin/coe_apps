#pragma once

#include <atomic_sync/types.h>
#include <lockwrapper/lockvka.h>

static int mutex_lock_generic(void *m) {
    return mutex_lock((mutex_t *) m);
}

static int mutex_unlock_generic(void *m) {
    return mutex_unlock((mutex_t *) m);
}

static inline lock_interface_t
make_lock_interface(mutex_t *mutex) {
    assert(mutex);
    lock_interface_t interface;
    interface.data = (void *) mutex;
    interface.mutex_lock = &mutex_lock_generic;
    interface.mutex_unlock = &mutex_unlock_generic;
    return interface;
}
