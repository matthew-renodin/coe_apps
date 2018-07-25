/*
 * Copyright 2018, Intelligent Automation, Inc.
 * This software was developed in part under Air Force contract number FA8750-15-C-0066 and DARPA 
 *  contract number 140D6318C0001.
 * This software was released under DARPA, public release number XXXXXXXX (to be updated once approved)
 * This software may be distributed and modified according to the terms of the BSD 2-Clause license.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND
 * FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY
 * WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * See "LICENSE_BSD2.txt" for details.
 *
 * @TAG(IAI_BSD)
 */
/**
 * @file sync.c
 * @brief Core implementation of libsync
 */

#include <atomic_sync/sync.h>
#include <atomic_sync/helpers.h>

#define NO_THREAD -1

int mutex_create(mutex_t *mutex, lock_type_t type) {
    if(mutex_set_type(mutex, LOCK_NONE) != LOCK_SUCCESS) { return LOCK_ERROR; }

    UNUSED int status;
    switch(type) {
    case LOCK_MUTEX_USERSPACE:
        return mutex_fast_init(mutex);

    case LOCK_RECURSIVE_USERSPACE:
        return mutex_fast_recursive_init(mutex);

    case LOCK_NOTIFICATION:
        if(!init_check_initialized()) {
            ZF_LOGE("Init data must be initialized");
            return LOCK_ERROR;
        }
        mutex_set_type(mutex, LOCK_NOTIFICATION);
        __atomic_store_n(&(mutex->can_destroy), true, __ATOMIC_SEQ_CST);
        status = sync_mutex_new(&init_objects.vka, &(mutex->notification_lock));
        return status == 0 ? LOCK_SUCCESS : LOCK_ERROR;

    case LOCK_NOTIFICATION_RECURSIVE:
        if(!init_check_initialized()) {
            ZF_LOGE("Init data must be initialized");
            return LOCK_ERROR;
        }
        mutex_set_type(mutex, LOCK_NOTIFICATION_RECURSIVE);
        __atomic_store_n(&(mutex->can_destroy), true, __ATOMIC_SEQ_CST);
        status = sync_recursive_mutex_new(&init_objects.vka, &(mutex->notification_recursive_lock));
        return status == 0 ? LOCK_SUCCESS : LOCK_ERROR;

    default:
        ZF_LOGE("Invalid lock type selected");
        return LOCK_ERROR;
    }
}

int mutex_fast_init(mutex_t *mutex){
    if(mutex_set_type(mutex, LOCK_MUTEX_USERSPACE) != LOCK_SUCCESS) { return LOCK_ERROR; }
    __atomic_store_n(&(mutex->fast_lock.value), 0, __ATOMIC_SEQ_CST);
    return LOCK_SUCCESS;
}

int mutex_fast_recursive_init(mutex_t *mutex) {
    if(mutex_set_type(mutex, LOCK_RECURSIVE_USERSPACE) != LOCK_SUCCESS) { return LOCK_ERROR; }
    __atomic_store_n(&(mutex->fast_recursive_lock.value), 0, __ATOMIC_SEQ_CST);
    __atomic_store_n(&(mutex->fast_recursive_lock.holder), NO_THREAD, __ATOMIC_SEQ_CST);
    return LOCK_SUCCESS;
}

int mutex_notification_init(mutex_t *mutex, seL4_CPtr notification) {
    if(mutex_set_type(mutex, LOCK_NOTIFICATION) != LOCK_SUCCESS) { return LOCK_ERROR; }
    __atomic_store_n(&(mutex->can_destroy), false, __ATOMIC_SEQ_CST);
    return sync_mutex_init(&(mutex->notification_lock), notification);
}

int mutex_recursive_init(mutex_t *mutex, seL4_CPtr notification){
    if(mutex_set_type(mutex, LOCK_NOTIFICATION_RECURSIVE) != LOCK_SUCCESS) { return LOCK_ERROR; }
    __atomic_store_n(&(mutex->can_destroy), false, __ATOMIC_SEQ_CST);
    return sync_recursive_mutex_init(&(mutex->notification_recursive_lock), notification);
}

static inline int
mutex_trylock(mutex_t *mutex){
    if (mutex == NULL) { 
        ZF_LOGE("Received a NULL lock");
        return LOCK_ERROR;
    }

    UNUSED int expected = 0;
    UNUSED int status = 0;
    switch(mutex->type) {
    case LOCK_MUTEX_USERSPACE:
        if (atomic_compare_exchange(&(mutex->fast_lock.value), &expected, 1)) {
            return LOCK_SUCCESS;
        } else {
            return LOCK_TRY_AGAIN;
        }

    case LOCK_RECURSIVE_USERSPACE: {
        int nil_holder = NO_THREAD;
        int expected_holder = thread_get_id();
        if (atomic_compare_exchange(&(mutex->fast_recursive_lock.holder), &nil_holder, thread_get_id())) {
            assert(mutex->fast_recursive_lock.value == 0);
            status = sync_atomic_increment_safe (&(mutex->fast_recursive_lock.value), &expected, __ATOMIC_SEQ_CST);
            return status == 0 ? LOCK_SUCCESS : LOCK_ERROR;
        }
        else if (atomic_compare_exchange(&(mutex->fast_recursive_lock.holder), &expected_holder, thread_get_id())) {
            status = sync_atomic_increment_safe (&(mutex->fast_recursive_lock.value), &expected, __ATOMIC_SEQ_CST);
            return status == 0 ? LOCK_SUCCESS : LOCK_ERROR;
        } else {
            return LOCK_TRY_AGAIN;
        }
    }

    case LOCK_NOTIFICATION:
        status = sync_mutex_lock(&(mutex->notification_lock));
        return status == 0 ? LOCK_SUCCESS : LOCK_ERROR;

    case LOCK_NOTIFICATION_RECURSIVE:
        status = sync_recursive_mutex_lock(&(mutex->notification_recursive_lock));
        return status == 0 ? LOCK_SUCCESS : LOCK_ERROR;

    default:
        ZF_LOGF("Invalid lock type selected: %d", mutex->type);
        return LOCK_ERROR;
    }
}

int mutex_lock(mutex_t *mutex) {
    int status;
    int i=100;
    while(1) {
        status = mutex_trylock(mutex);
        if(status != LOCK_TRY_AGAIN) {
            return status;
        }
        if(i > 0) {
            i--;
            seL4_Yield();
        } else {
            seL4_Sleep(1000);
        }
    }
    return LOCK_SUCCESS;
}

int mutex_unlock(mutex_t *mutex) {
    UNUSED int expected = 1;
    UNUSED int status = 0;
    switch(mutex->type) {
    case LOCK_MUTEX_USERSPACE:
        if(atomic_compare_exchange(&(mutex->fast_lock.value), &expected, 0)) {
            return LOCK_SUCCESS;
        }
        ZF_LOGE("Internal lock value is unexpected. Perhaps the lock is corrupted or initialized?");
        return LOCK_ERROR;

    case LOCK_RECURSIVE_USERSPACE: {
        int expected_holder = thread_get_id();
        if(atomic_compare_exchange(&(mutex->fast_recursive_lock.holder), &expected_holder, thread_get_id())) {
            if(atomic_compare_exchange(&(mutex->fast_lock.value), &expected, 0)) {
                atomic_compare_exchange(&(mutex->fast_recursive_lock.holder), &expected_holder, NO_THREAD);
                return LOCK_SUCCESS;
            } else {
                status = sync_atomic_decrement_safe (&(mutex->fast_recursive_lock.value), &expected, __ATOMIC_SEQ_CST);
                return status == 0 ? LOCK_SUCCESS : LOCK_ERROR;
            }
        } else {
            ZF_LOGE("Tried to unlock re-entrant lock without being the holder");
            return LOCK_ERROR;
        }
        }

    case LOCK_NOTIFICATION:
        status = sync_mutex_unlock(&(mutex->notification_lock));
        return status == 0 ? LOCK_SUCCESS : LOCK_ERROR;

    case LOCK_NOTIFICATION_RECURSIVE:
        status = sync_recursive_mutex_unlock(&(mutex->notification_recursive_lock));
        return status == 0 ? LOCK_SUCCESS : LOCK_ERROR;

    default:
        ZF_LOGF("Invalid lock type selected");
        return LOCK_ERROR;
    }
}

int mutex_destroy(mutex_t *mutex){
    
    UNUSED int status = LOCK_SUCCESS;
    switch(mutex->type){
        case LOCK_NOTIFICATION:
            if(mutex->can_destroy) {
                status = sync_mutex_destroy(&(init_objects.vka), &(mutex->notification_lock));
            } else {
                ZF_LOGD("Destroying a lock not initialized by mutex_create will not free internal data.\n");
            }
            break;
        case LOCK_NOTIFICATION_RECURSIVE:
            if(mutex->can_destroy) {
                status = sync_recursive_mutex_destroy(&(init_objects.vka), &(mutex->notification_recursive_lock));
            } else {
                ZF_LOGD("Destroying a lock not initialized by mutex_create will not free internal data.\n");
            }
            break;
        default:
            break;
    }
    if (status != LOCK_SUCCESS) { return status; }
    __atomic_store_n(&(mutex->type), LOCK_NONE, __ATOMIC_SEQ_CST);
    return LOCK_SUCCESS;
}

int cond_init(cond_t* cond, lock_type_t lock_type) {
    if (cond == NULL || lock_type == LOCK_NONE) { return LOCK_ERROR; }

    UNUSED int status = LOCK_SUCCESS;
    mutex_t *mutex_ptr = (mutex_t *) malloc(sizeof(mutex_t));
    if (mutex_ptr == NULL) { return LOCK_ERROR; }
    
    status = mutex_create(mutex_ptr, lock_type);
    status = cond_attach(cond, mutex_ptr);

    cond->can_destroy_main_lock = true;
    if(status == LOCK_SUCCESS) {
        cond->main_lock = mutex_ptr;
    } else {
        free(mutex_ptr);
    }
    return status;
}

int cond_attach(cond_t *cond, mutex_t *lock) {
    if (cond == NULL || lock == NULL) { 
        ZF_LOGE_IF(cond == NULL, "Received a NULL condition variable");
        ZF_LOGE_IF(lock == NULL, "Received a NULL lock");
        return LOCK_ERROR; 
    }

    UNUSED int status = LOCK_SUCCESS;
    cond->main_lock = lock;
    status = mutex_create(&(cond->queue_lock), LOCK_MUTEX_USERSPACE);
    cond->queue_head = NULL;
    cond->queue_tail = NULL;
    cond->can_destroy_main_lock = false;
    return status;
}

int cond_lock_acquire(cond_t *cond) {
    return mutex_lock(cond->main_lock);
}

int cond_lock_release(cond_t *cond) {
    return mutex_unlock(cond->main_lock);
}

static inline void
cond_queue_lock(cond_t *cond) {
    int status = mutex_lock(&(cond->queue_lock));
    ZF_LOGF_IF(status != LOCK_SUCCESS, "CV failed to acquire queue lock");
}

static inline void 
cond_queue_unlock(cond_t *cond) {
    int status = mutex_unlock(&(cond->queue_lock));
    ZF_LOGF_IF(status != LOCK_SUCCESS, "CV failed to release queue lock");
}

int cond_wait(cond_t* cond) {
    UNUSED int status = LOCK_SUCCESS;
    
    struct tcb_queue_node waitNode;
    waitNode.notification = thread_get_sync_notification();

    #ifdef CONFIG_DEBUG_BUILD
    /* Check the cap actually is a notification. */
    ZF_LOGF_IF(seL4_DebugCapIdentify(waitNode.notification) != 6, "Thread %d has wrong cap type: %lu", thread_get_id(), (unsigned long) seL4_DebugCapIdentify(waitNode.notification));
    #endif

    cond_queue_lock(cond);
    condition_waiters_enqueue(cond, &waitNode);
    cond_queue_unlock(cond);
    
    cond_lock_release(cond);
    seL4_Wait(waitNode.notification, NULL);
    cond_lock_acquire(cond);
    
    return status;
}

/* 
 * Convenience Funtion
 * Precondition: Caller is holding cond->queue_lock
 * Returns true if signal sent 
 *  and false if there are no receivers
 */
static inline bool
signal_once(cond_t* cond) {
    tcb_queue_t signal_node = NULL;
    condition_waiters_dequeue(cond, &signal_node);
    if (signal_node != NULL) {
        seL4_Signal(signal_node->notification);
        return true;
    }
    return false;
}

int cond_signal(cond_t* cond) {

    cond_queue_lock(cond);    
    signal_once(cond);
    cond_queue_unlock(cond);

    return LOCK_SUCCESS;
}

int cond_broadcast(cond_t* cond) {

    cond_queue_lock(cond);
    while(signal_once(cond));
    cond_queue_unlock(cond);
    
    return LOCK_SUCCESS;
}

int cond_destroy(cond_t* cond){
    if (cond->can_destroy_main_lock) {
        mutex_destroy(cond->main_lock);
        free(cond->main_lock);
    } else {
        ZF_LOGD("Destroying a condition variable not initialized by cond_init will not free internal data.\n");
    }
    cond->main_lock = NULL;
    mutex_destroy(&(cond->queue_lock));
    cond->queue_head = NULL;
    cond->queue_tail = NULL;
    return LOCK_SUCCESS;
}