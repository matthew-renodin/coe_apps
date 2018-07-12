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

typedef struct userspace_spinlock {
    volatile int value;
} ulock_t;

typedef struct userspace_spinlock_recursive {
    volatile int value;
    volatile seL4_Word holder;
} ulock_recursive_t;

typedef enum {
    LOCK_NONE = 0,
    LOCK_MUTEX_USERSPACE,
    LOCK_RECURSIVE_USERSPACE,
    LOCK_NOTIFICATION,
    LOCK_NOTIFICATION_RECURSIVE
} lock_type_t;

typedef struct mutex {
    lock_type_t type;
    union {
        ulock_t fast_lock;
        ulock_recursive_t fast_recursive_lock;
        sync_mutex_t notification_lock;
        sync_recursive_mutex_t notification_recursive_lock;
    };
    bool can_destroy;
} mutex_t;

typedef struct tcb_queue_node* tcb_queue_t;

struct tcb_queue_node {
    seL4_CPtr notification;
    tcb_queue_t next;
};

typedef struct userspace_cond cond_t;

struct userspace_cond {
    mutex_t *main_lock;
    mutex_t queue_lock;
    tcb_queue_t queue_head;
    tcb_queue_t queue_tail;
    bool can_destroy_main_lock;
};
