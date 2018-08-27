/*
 * Copyright 2018, Intelligent Automation, Inc.
 * This software was developed in part under Air Force contract number FA8750-15-C-0066 and DARPA 
 *  contract number 140D6318C0001.
 * This software was released under DARPA, public release number 0.6.
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
 * @file types.h
 * @brief Exported type definitions for libsync
 */

#pragma once

#include <stdbool.h>

#include <sel4/sel4.h>
#include <vka/vka.h>

#include <init/init.h>
#include <sync/mutex.h>
#include <sync/recursive_mutex.h>

#define LOCK_TRY_AGAIN 1
#define LOCK_SUCCESS 0
#define LOCK_ERROR -1

/* See warning below before using ticket lock */
/* #define TICKET_LOCK 1 */

#ifdef TICKET_LOCK
/**
 * @brief Basic spinlock using ticket locking algorithm
 *
 * @warning Do not use ticket locking with thread destruction
 * as thread destruction on a waiter causes deadlock on that lock 
 */
typedef struct userspace_spinlock {
    volatile int next_ticket;
    volatile int now_serving;
} spinlock_t;

#else
/**
 * @brief basic spinlock
 */
typedef struct userspace_spinlock {
    volatile int value;
} spinlock_t;

#endif /* TICKET_LOCK */

/**
 * @brief Recursive spinlocking primatives.
 */
typedef struct userspace_spinlock_recursive {
    spinlock_t lock;
    volatile int held;
    volatile int holder;
} spinlock_recursive_t;

/**
 * @brief Possible lock types for mutex_t
 * 
 * LOCK_NONE                    Default (0) for uninitialized locks
 * LOCK_SPINLOCK                Non-recursive userspace spinlocks (no untyped needed)
 * LOCK_SPINLOCK_RECURSIVE      Recursive (re-entrant) userspace spinlocks (no untyped needed)
 * LOCK_NOTIFICATION            Notification based spinlocks 
 *                              (Requires notification endpoint or init_objects.vka with untyped memory)
 * LOCK_NOTIFICATION_RECURSIVE  Notification-based recursive spinlocks 
 *                              (Requires notification endpoint or init_objects.vka with untyped memory)
 */
typedef enum {
    LOCK_NONE = 0,
    LOCK_SPINLOCK,
    LOCK_SPINLOCK_RECURSIVE,
    LOCK_NOTIFICATION,
    LOCK_NOTIFICATION_RECURSIVE
} lock_type_t;

/**
 * @brief Mutex object abstracts away from specific lock implementations.
 */
typedef struct mutex {
    lock_type_t type;
    union {
        spinlock_t spinlock;
        spinlock_recursive_t spinlock_recursive;
        sync_mutex_t notification_lock;
        sync_recursive_mutex_t notification_recursive_lock;
    };
    bool can_destroy;
} mutex_t;

/**
 * @brief Queue for threads waiting on a condition varaible.
 */
typedef struct tcb_queue_node* tcb_queue_t;
struct tcb_queue_node {
    seL4_CPtr notification;
    tcb_queue_t next;
};

/**
 * @brief Condition Variable 
 * 
 * @warning Requires thread_local_storage from libinit, BUT does not need extra untyped memory
 */
typedef struct userspace_cond cond_t;
struct userspace_cond {
    mutex_t *main_lock;
    mutex_t queue_lock;
    tcb_queue_t queue_head;
    tcb_queue_t queue_tail;
    bool can_destroy_main_lock;
};
