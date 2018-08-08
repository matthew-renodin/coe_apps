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

int cond_init(cond_t* cond, lock_type_t lock_type) {
    if (cond == NULL || lock_type == LOCK_NONE) { return LOCK_ERROR; }
    UNUSED int status = LOCK_SUCCESS;

    mutex_t *mutex_ptr = (mutex_t *) malloc(sizeof(mutex_t));
    if (mutex_ptr == NULL) { goto error; }
    
    status = mutex_create(mutex_ptr, lock_type);
    if(status != LOCK_SUCCESS) { goto error; }

    status = cond_attach(cond, mutex_ptr);
    if(status != LOCK_SUCCESS) { goto error; }

    cond->can_destroy_main_lock = true;
    cond->main_lock = mutex_ptr;

    return status;
    error:
        if(mutex_ptr != NULL) {free(mutex_ptr); }
    return LOCK_ERROR;
}

int cond_attach(cond_t *cond, mutex_t *lock) {
    if (cond == NULL || lock == NULL) { 
        ZF_LOGE_IF(cond == NULL, "Received a NULL condition variable");
        ZF_LOGE_IF(lock == NULL, "Received a NULL lock");
        return LOCK_ERROR; 
    }

    UNUSED int status = LOCK_SUCCESS;
    cond->main_lock = lock;
    status = mutex_create(&(cond->queue_lock), LOCK_SPINLOCK);
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
