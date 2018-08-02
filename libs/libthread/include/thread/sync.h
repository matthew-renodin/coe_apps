/**
 * @file sync.h
 * @brief Provide locking mechanism for libthread
 */

#pragma once

#include <thread/thread.h>
#include <atomic_sync/helpers.h>

extern int thread_lib_lock_initialized;
extern mutex_t thread_lib_lock;

/******************************************************************************
 *  Commands for dealing with the thread_lib_lock
 *****************************************************************************/

static inline void libthread_lock_init() {
    int expected = 0;
    int error = 0;
    if (unlikely(atomic_compare_exchange_int(&thread_lib_lock_initialized, &expected, -1))) {
        error = mutex_create(&thread_lib_lock, LOCK_RECURSIVE_USERSPACE);
        ZF_LOGF_IF(error, "Failed to initialize libthread lock");
        __atomic_store_n(&thread_lib_lock_initialized, 1, __ATOMIC_SEQ_CST);
    }
    
    while( unlikely(thread_lib_lock_initialized != 1) ) {
        seL4_Yield();
    }
}

static inline void 
libthread_lock_acquire() {
    libthread_lock_init();
    mutex_lock(&thread_lib_lock);
}
    

static inline void 
libthread_lock_release()
{
    mutex_unlock(&thread_lib_lock);
}

static inline bool holding_libthread_lock() {
    libthread_lock_init();
    return (thread_get_id() == thread_lib_lock.fast_recursive_lock.holder);
}

static inline void libthread_condition_variable_init(thread_handle_t *handle) {
    if( unlikely(!handle->join_condition_initialized) ) {
        cond_attach(&handle->join_condition, &thread_lib_lock);
        handle->join_condition_initialized = true;
    }
}

/******************************************************************************
 *  Prologue sets up status and locks, 
 *  status commands modify status
 *  epilogue returns
 *****************************************************************************/

#define libthread_prologue(return_type, def_val) \
libthread_lock_acquire(); \
return_type _libthread_status = def_val

#define libthread_set_status(condition) \
_libthread_status = (condition);

#define libthread_get_status() _libthread_status

#define libthread_guard(condition, error_status, error_symbol, ...) \
if (unlikely((condition))) { \
    _libthread_status = (error_status);\
    ZF_LOGE(__VA_ARGS__);\
    goto error_symbol;\
}

#define libthread_custom_epilogue() \
libthread_epilogue:

#define libthread_return_value(value) \
    libthread_lock_release(); \
    return value;

#define libthread_return_success() \
    libthread_return_value(0);

#define libthread_epilogue() \
libthread_custom_epilogue() \
    libthread_return_value(_libthread_status)

/**
 *  Convenience Functions
 **/

#define libthread_check_initialized(fail_return) \
    libthread_guard(!init_check_initialized(), fail_return, libthread_epilogue, \
                    "Init objects (vka, vspace) have not been setup.\n" \
                    "Run init_process or init_root_task to setup."); \
    libthread_guard(!init_has_untypeds(), fail_return, libthread_epilogue, \
                    "This process has not been allocated untyped memory,\n" \
                    "which is necessary for thread creation.")
    