#include <atomic_sync/interface.h>
#include <atomic_sync/prototypes.h>

/**
 * @brief Basic wrapper around lock - should not be called directly
 */
static int mutex_lock_generic(void *m) {
    return mutex_lock((mutex_t *) m);
}

/**
 * @brief Basic wrapper around unlock - should not be called directly
 */
static int mutex_unlock_generic(void *m) {
    return mutex_unlock((mutex_t *) m);
}

lock_interface_t make_lock_interface(mutex_t *mutex) {
    assert(mutex);
    lock_interface_t interface;
    interface.data = (void *) mutex;
    interface.mutex_lock = &mutex_lock_generic;
    interface.mutex_unlock = &mutex_unlock_generic;
    return interface;
}