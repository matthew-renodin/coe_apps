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

/*
 *  Mutex Locks
 */
/* Initialize Mutex and Allocate Members */
int mutex_create(mutex_t *mutex, lock_type_t type) {
    if(mutex_set_type(mutex, LOCK_NONE) != LOCK_SUCCESS) { return LOCK_ERROR; }
    switch(type) {
    case LOCK_MUTEX_USERSPACE:
        return mutex_fast_init(mutex);
    case LOCK_RECURSIVE_USERSPACE:
        return mutex_recursive_init(mutex);
    case LOCK_NOTIFICATION:
        mutex_set_type(mutex, LOCK_NOTIFICATION);
        __atomic_store_n(&(mutex->can_destroy), true, __ATOMIC_SEQ_CST);
        return sync_mutex_new(&init_objects.vka, &(mutex->notification_lock));
    case LOCK_NOTIFICATION_RECURSIVE:
        mutex_set_type(mutex, LOCK_NOTIFICATION_RECURSIVE);
        __atomic_store_n(&(mutex->can_destroy), true, __ATOMIC_SEQ_CST);
        return sync_recursive_mutex_new(&init_objects.vka, &(mutex->notification_recursive_lock));
    default:
        return LOCK_ERROR;
    }
}

/* Initialize Mutex with pre-allocated members */
int mutex_fast_init(mutex_t *mutex){
    if(mutex_set_type(mutex, LOCK_MUTEX_USERSPACE) != LOCK_SUCCESS) { return LOCK_ERROR; }
    __atomic_store_n(&(mutex->fast_lock.value), 0, __ATOMIC_SEQ_CST);
    return LOCK_SUCCESS;
}

int mutex_fast_recursive_init(mutex_t *mutex) {
    if(mutex_set_type(mutex, LOCK_RECURSIVE_USERSPACE) != LOCK_SUCCESS) { return LOCK_ERROR; }
    __atomic_store_n(&(mutex->fast_recursive_lock.value), 0, __ATOMIC_SEQ_CST);
    __atomic_store_n(&(mutex->fast_recursive_lock.holder), 0, __ATOMIC_SEQ_CST);
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
    return sync_mutex_init(&(mutex->notification_recursive_lock), notification);
}

/* Lock and Unlock Mutex Members */
static inline int
mutex_trylock(mutex_t *mutex){
    if (mutex == NULL) { return LOCK_ERROR; }
    UNUSED int expected = 0;
    switch(mutex->type) {
    case LOCK_MUTEX_USERSPACE:
        if (atomic_compare_exchange(&(mutex->fast_lock.value), &_expected, 1)) {
            return LOCK_SUCCESS;
        }
        break;
    case LOCK_RECURSIVE_USERSPACE:
        seL4_Word nil_holder = (seL4_Word) NULL;
        seL4_Word expected_holder = thread_id();
        if (atomic_compare_exchange(&(mutex->fast_recursive_lock.holder), &nil_holder, thread_id()) ||
            atomic_compare_exchange(&(mutex->fast_recursive_lock.holder), &expected_holder, thread_id())) {
            __atomic_add_fetch (&(mutex->fast_recursive_lock.value), 1, __ATOMIC_ACQ_REL);
            return LOCK_SUCCESS;
        }
        break;
    case LOCK_NOTIFICATION:
        return sync_mutex_lock(mutex->notification_lock);
    case LOCK_NOTIFICATION_RECURSIVE:
        return sync_recursive_mutex_lock(mutex->notification_recursive_lock);
    default:
        return LOCK_ERROR;
    }
    return LOCK_TRY_AGAIN;
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
    switch(mutex->type) {
    case LOCK_MUTEX_USERSPACE:
        if(atomic_compare_exchange(&(mutex->fast_lock.value), &expected, 0)) {
            return LOCK_SUCCESS;
        }
        return LOCK_ERROR; /* Was lock unlocked or was value not 0/1 */
    case LOCK_RECURSIVE_USERSPACE:
        seL4_Word expected_holder = thread_id();
        if(atomic_compare_exchange(&(mutex->fast_recursive_lock.holder), &expected_holder, thread_id())) {
            if(atomic_compare_exchange(&(mutex->fast_lock.value), &expected, 0)) {
                atomic_compare_exchange(&(mutex->fast_recursive_lock.holder), &expected_holder, thread_id())
            } else {
                __atomic_sub_fetch (&(mutex->fast_recursive_lock.value), 1, __ATOMIC_ACQ_REL);
            }
            return LOCK_SUCCESS;
        } else {
            return LOCK_ERROR;
        }
    case LOCK_NOTIFICATION:
        return sync_mutex_unlock(&(mutex->notification_lock));
    case LOCK_NOTIFICATION_RECURSIVE:
        return sync_recursive_mutex_unlokc(&(mutex->notification_recursive_lock));
    default:
        return LOCK_ERROR;
    }
}

/* De-initialize mutex and destroy members (caution for use with pre-allocated members) */
int mutex_destroy(mutex_t *mutex){
    if(mutex->can_destroy) {
        UNUSED int status = LOCK_SUCCESS;
        switch(mutex->type){
            case LOCK_NOTIFICATION:
                status = sync_mutex_destroy(&(init_objects.vka), &(mutex->notification_lock));
                break;
            case LOCK_NOTIFICATION_RECURSIVE:
                status = sync_recursive_mutex_destroy(&(init_objects.vka), &(mutex->notification_recursive_lock));
                break;
        }
        if (status != LOCK_SUCCESS) { return status; }
    }
    __atomic_store_n(&(mutex->type), LOCK_NONE, __ATOMIC_SEQ_CST);
    return LOCK_SUCCESS;
}

/*
 *  Conditional Variables
 */
/* Initialize CV and Allocate Members */
int cond_init(cond_t* cond, lock_type_t lock_type) {
    UNUSED status = LOCK_SUCCESS;
    if (cond == NULL) {
        return LOCK_ERROR;
    }
    if(lock_type == LOCK_NONE) { lock_type = LOCK_MUTEX_USERSPACE; }
    status = mutex_create(cond->main_lock, lock_type);
    status = mutex_create(cond->queue_lock, LOCK_MUTEX_USERSPACE);
    cond->queue_head = NULL;
    cond->queue_tail = NULL;
    return LOCK_SUCCESS;
}

/* CV Wait and Signal */
int cond_lock_acquire(cond_t *cond) {
    return mutex_lock(cond->main_lock);
}

int cond_wait(cont_t* cond) {
    struct tcb_queue_node waitNode;
    if(get_lock_notification(&(waitNode.notification)) != LOCK_SUCCESS) {
        return LOCK_ERROR;
    }
    mutex_lock(cond->queue_lock);
    condition_waiters_enqueue(cond, &waitNode);
    mutex_unlock(cond->queue_lock);
    
    mutex_unlock(cond->main_lock);
    seL4_Wait(waitNode.notification, NULL);
    mutex_lock(cond->main_lock);
    return LOCK_SUCCESS;
}

/* 
 * Convenience Funtion
 * Precondition: Caller is holding cond->queue_lock
 * Returns true if signal sent 
 *  and false if there are no receivers
 */
static inline bool
signal_once(cond_t* cond) {
    tcb_queue_t signal_node;
    condition_waiters_dequeue(cond, &signal_node);
    if (signal_node != NULL) {
        seL4_Signal(signal_node->notification);
        return true;
    }
    return false;
}

int cond_signal(cond_t* cond) {
    mutex_lock(cond->queue_lock);
    signal_once(cond);
    mutex_unlock(cond->queue_lock);
}

int cond_broadcast(cond_t* cond) {
    mutex_lock(cond->queue_lock);
    while(signal_once(cond));
    mutex_unlock(cond->queue_lock);
}

int cond_lock_release(cond_t *cond) {
    return mutex_unlock(cond->main_lock);
}

/* De-initalize CV and Destroy members */
int cond_destroy(cond_t* cond){
    mutex_destroy(cond->main_lock);
    mutex_destroy(cond->queue_lock);
    queue_head = NULL;
    queue_tail = NULL;
}