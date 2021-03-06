#pragma once

#include <thread/thread.h>
#include <atomic_sync/sync.h>
#include <atomic_sync/helpers.h>

#include <process/process.h>
#include <process/errors.h>

extern int process_lib_lock_initialized;
extern mutex_t process_lib_lock;

/******************************************************************************
 *  Commands for dealing with the process_lib_lock
 *****************************************************************************/

static inline void libprocess_lock_init() {
    int expected = 0;
    int error = 0;
    if (unlikely(atomic_compare_exchange_int(&process_lib_lock_initialized, &expected, -1))) {
        error = mutex_notification_init(&process_lib_lock, init_objects.process_lock_cap, true);
        ZF_LOGF_IF(error, "Failed to initialize libprocess lock");
        __atomic_store_n(&process_lib_lock_initialized, 1, __ATOMIC_SEQ_CST);
    }
    
    while( unlikely(process_lib_lock_initialized != 1) ) {
        seL4_Yield();
    }
}

static inline void 
libprocess_lock_acquire() {
    libprocess_lock_init();
    mutex_lock(&process_lib_lock);
}

static inline void 
libprocess_lock_release() {
    mutex_unlock(&process_lib_lock);
}

static inline bool holding_libprocess_lock() {
    libprocess_lock_init();
    return (seL4_GetIPCBuffer() == process_lib_lock.notification_recursive_lock.owner);
}

/******************************************************************************
 *  Prologue sets up status and locks, 
 *  status commands modify status
 *  epilogue returns
 *****************************************************************************/

#define libprocess_prologue() \
libprocess_lock_acquire(); \
int _libprocess_status = 0

#define libprocess_set_status(condition) \
_libprocess_status = (condition);

#define libprocess_get_status() _libprocess_status

#define libprocess_guard(condition, error_status, error_symbol, ...) \
if (unlikely((condition))) { \
    _libprocess_status = (error_status);\
    ZF_LOGE(__VA_ARGS__);\
    goto error_symbol;\
}

#define libprocess_custom_epilogue() libprocess_epilogue:

#define libprocess_return_value(value) \
    libprocess_lock_release(); \
    return value;

#define libprocess_return_success() \
    libprocess_return_value(0);

#define libprocess_epilogue() \
libprocess_custom_epilogue() \
    libprocess_return_value(_libprocess_status)

/******************************************************************************
 * Convenience Checks
 *****************************************************************************/
#define libprocess_error_guard(condition, error, error_symbol) \
    libprocess_guard(condition, error##_NUMBER, error_symbol, error##_STRING)

#define libprocess_check_initialized() \
    libprocess_error_guard(!init_check_initialized(), INITIALIZATION_ERROR, libprocess_epilogue); \
    libprocess_error_guard(!init_has_untypeds(), UNTYPEDS_ERROR, libprocess_epilogue);

#define libprocess_check_arg(arg) \
    libprocess_error_guard((arg) == NULL, NULL_ARG_ERROR, libprocess_epilogue)

#define libprocess_check_malloc(obj, symbol) \
    libprocess_error_guard((obj) == NULL, MALLOC_ERROR, symbol)

#define libprocess_check_state(handle, required_state) \
    libprocess_error_guard(handle->state != required_state, STATE_ERROR, libprocess_epilogue)
