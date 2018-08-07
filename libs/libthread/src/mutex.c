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

/**
 *  Internal Functions
 **/

#ifdef TICKET_LOCK
static int ticketlock_init(spinlock_t *lock) {
    __atomic_store_n(&(lock->next_ticket), 0, __ATOMIC_SEQ_CST);
    __atomic_store_n(&(lock->now_serving), 0, __ATOMIC_SEQ_CST);
    return LOCK_SUCCESS;
}

static int ticketlock_trylock(spinlock_t *lock) {
    int my_ticket = __atomic_fetch_add(&lock->next_ticket, 1, __ATOMIC_SEQ_CST);
    while(my_ticket != lock->now_serving);
    return LOCK_SUCCESS;
}

static int ticketlock_unlock(spinlock_t *lock) {
    __atomic_fetch_add(&lock->now_serving, 1, __ATOMIC_SEQ_CST);
    return LOCK_SUCCESS;
}
#else
static int spinlock_init(spinlock_t *lock) {
    __atomic_store_n(&(lock->value), 0, __ATOMIC_SEQ_CST);
    return LOCK_SUCCESS;
}

static int spinlock_trylock(spinlock_t *lock) {
    int expected = 0;
    if (atomic_compare_exchange(&(lock->value), &expected, 1)) {
        return LOCK_SUCCESS;
    } else {
        return LOCK_TRY_AGAIN;
    }
}

static int spinlock_unlock(spinlock_t *lock) {
    int expected = 1;
    if(atomic_compare_exchange(&(lock->value), &expected, 0)) {
        return LOCK_SUCCESS;
    }
    ZF_LOGE("Internal lock value is unexpected. Perhaps the lock is corrupted or initialized?");
    return LOCK_ERROR;
}
#endif


#ifdef TICKET_LOCK
#define internal_init(...) ticketlock_init(__VA_ARGS__)
#define internal_trylock(...) ticketlock_trylock(__VA_ARGS__)
#define internal_unlock(...) ticketlock_unlock(__VA_ARGS__)
#else
#define internal_init(...) spinlock_init(__VA_ARGS__)
#define internal_trylock(...) spinlock_trylock(__VA_ARGS__)
#define internal_unlock(...) spinlock_unlock(__VA_ARGS__)
#endif

/**
 *  API Implementation
 **/

int mutex_create(mutex_t *mutex, lock_type_t type) {
    if(mutex_set_type(mutex, LOCK_NONE) != LOCK_SUCCESS) { return LOCK_ERROR; }

    UNUSED int status;
    switch(type) {
    case LOCK_SPINLOCK:
    case LOCK_SPINLOCK_RECURSIVE:
        return mutex_spinlock_init(mutex, type == LOCK_SPINLOCK_RECURSIVE);

    case LOCK_NOTIFICATION:
    case LOCK_NOTIFICATION_RECURSIVE:
        if(!init_check_initialized()) {
            ZF_LOGE("Init data must be initialized");
            return LOCK_ERROR;
        }
        mutex_set_type(mutex, type);
        __atomic_store_n(&(mutex->can_destroy), true, __ATOMIC_SEQ_CST);

        if(type == LOCK_NOTIFICATION) { status = sync_mutex_new(&init_objects.vka, &(mutex->notification_lock)); }
        else { status = sync_recursive_mutex_new(&init_objects.vka, &(mutex->notification_recursive_lock)); }
        return status == 0 ? LOCK_SUCCESS : LOCK_ERROR;

    default:
        ZF_LOGE("Invalid lock type selected");
        return LOCK_ERROR;
    }
}

int mutex_spinlock_init(mutex_t *mutex, bool recursive){
    lock_type_t requested_type = recursive ? LOCK_SPINLOCK_RECURSIVE : LOCK_SPINLOCK;
    if(mutex_set_type(mutex, requested_type) != LOCK_SUCCESS) { return LOCK_ERROR; }
    if (recursive) {
        __atomic_store_n(&(mutex->spinlock_recursive.held), 0, __ATOMIC_SEQ_CST);
        __atomic_store_n(&(mutex->spinlock_recursive.holder), NO_THREAD, __ATOMIC_SEQ_CST);
    }
    spinlock_t * internal_lock = recursive ? &(mutex->spinlock_recursive.lock) : &mutex->spinlock;
    return internal_init(internal_lock);
}

int mutex_notification_init(mutex_t *mutex, seL4_CPtr notification, bool recursive) {
    lock_type_t requested_type = recursive ? LOCK_NOTIFICATION_RECURSIVE : LOCK_NOTIFICATION;
    if(mutex_set_type(mutex, requested_type) != LOCK_SUCCESS) { return LOCK_ERROR; }
    __atomic_store_n(&(mutex->can_destroy), false, __ATOMIC_SEQ_CST);

    int status = 0;
    if (recursive) { status = sync_recursive_mutex_init(&(mutex->notification_recursive_lock), notification); }
    else { status = sync_mutex_init(&(mutex->notification_lock), notification); }

    return status == 0 ? LOCK_SUCCESS : LOCK_ERROR;
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
    case LOCK_SPINLOCK:
        return internal_trylock(&(mutex->spinlock));

    case LOCK_SPINLOCK_RECURSIVE: {
        int nil_holder = NO_THREAD;
        int expected_holder = thread_get_id();
        if (atomic_compare_exchange(&(mutex->spinlock_recursive.holder), &expected_holder, thread_get_id())) {
            assert(mutex->spinlock_recursive.held > 0);
            status = sync_atomic_increment_safe (&(mutex->spinlock_recursive.held), &expected, __ATOMIC_SEQ_CST);
            return status == 0 ? LOCK_SUCCESS : LOCK_ERROR;
        }
        status = internal_trylock(&(mutex->spinlock_recursive.lock));
        if (status == LOCK_SUCCESS) {
            atomic_compare_exchange(&(mutex->spinlock_recursive.holder), &nil_holder, thread_get_id());
            __atomic_store_n(&(mutex->spinlock_recursive.held), 1, __ATOMIC_SEQ_CST);
        }
        return status;
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
    int status = LOCK_TRY_AGAIN;
    while(status == LOCK_TRY_AGAIN) {
        status = mutex_trylock(mutex);
    }
    return status;
}

int mutex_unlock(mutex_t *mutex) {
    int status = 0;
    switch(mutex->type) {
    case LOCK_SPINLOCK:
        return internal_unlock(&mutex->spinlock);

    case LOCK_SPINLOCK_RECURSIVE: {
        int expected = 1;
        int expected_holder = thread_get_id();
        if(atomic_compare_exchange(&(mutex->spinlock_recursive.holder), &expected_holder, thread_get_id())) {
            if(atomic_compare_exchange(&(mutex->spinlock_recursive.held), &expected, 0)) {
                mutex->spinlock_recursive.holder = NO_THREAD;
                return internal_unlock(&(mutex->spinlock_recursive.lock));
            } else {
                status = sync_atomic_decrement_safe (&(mutex->spinlock_recursive.held), &expected, __ATOMIC_SEQ_CST);
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