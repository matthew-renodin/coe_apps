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
/*
 * @file helpers.h
 * @brief Internal helpers for atomic_sync
 */

#pragma once

#include <sel4/sel4.h>
#include <thread/thread.h>
#include <atomic_sync/types.h>

#define atomic_compare_exchange(lock, expected, value) __atomic_compare_exchange_n((lock), expected, (value), 0, __ATOMIC_SEQ_CST, __ATOMIC_RELAXED)

/**
 * It's probably better to use the typed variants of these 
 * so that you'll at least get a warning about the conversions
 * because passing the wrong types to the atomic functions can really mess up the code
 **/
static inline bool
atomic_compare_exchange_int(int *lock, int *expected, int value) {
    return atomic_compare_exchange(lock, expected, value);
}

static inline bool
atomic_compare_exchange_word(seL4_Word *lock, seL4_Word *expected, seL4_Word value) {
    return atomic_compare_exchange(lock, expected, value);
}

/* convenience function */
static inline int
mutex_set_type(mutex_t *mutex, lock_type_t type) {
    if (mutex == NULL) { 
        return LOCK_ERROR; 
    }
    __atomic_store_n(&(mutex->type), type, __ATOMIC_SEQ_CST);
    return LOCK_SUCCESS;
}

/* 
 * Waiters enqueue and dequeue have the precondition that
 * the queue lock is held before use to avoid race conditions
 */
static inline void
condition_waiters_enqueue(cond_t* cond, tcb_queue_t node) {
    if (cond == NULL || node == NULL) { return; }
    if(cond->queue_head == NULL) {
        cond->queue_head = node;
    } else {
        cond->queue_tail->next = node;
    }
    cond->queue_tail = node;
    node->next = NULL;
}

static inline void
condition_waiters_dequeue(cond_t* cond, tcb_queue_t* node) {
    if(node == NULL) { return; }
    if(cond == NULL || cond->queue_head == NULL) { *node = NULL; return; }
    *node = cond->queue_head;
    cond->queue_head = cond->queue_head->next;
    (*node)->next = NULL;
    if(cond->queue_head == NULL) {
        cond->queue_tail = NULL;
    }
}