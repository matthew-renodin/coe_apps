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
 * @file prototypes.h
 * @brief Exported prototype definitions for libsync
 */

#pragma once

#include <atomic_sync/types.h>

/*
 *  Mutex Locks
 */
/* Initialize Mutex and Allocate Members */
int mutex_create(mutex_t *mutex, lock_type_t type);

/* Initialize Mutex with pre-allocated members */
int mutex_fast_init(mutex_t *mutex);
int mutex_fast_recursive_init(mutex_t *mutex);
int mutex_notification_init(mutex_t *mutex, seL4_CPtr notification);
int mutex_notification_recursive_init(mutex_t *mutex, seL4_CPtr notification);

/* Lock and Unlock Mutex Members */
int mutex_lock(mutex_t *mutex);
int mutex_unlock(mutext_t *mutex);

/* De-initialize mutex and destroy members (caution for use with pre-allocated members) */
int mutex_destroy(mutex_t *mutex);

/*
 *  Conditional Variables
 */
/* Initialize CV and Allocate Members */
int cond_init(cond_t *cond, lock_type_t lock_type);

/* CV Acquires and releases main_lock */
int cond_lock_acquire(cond_t *cond);
int cond_lock_release(cond_t *cond);

/* CV Wait and Signal */
int cond_wait(cont_t *cond);  /* Precondition: Caller holds main_lock */ 
int cond_signal(cond_t *cond);
int cond_broadcast(cond_t *cond);
#define cond_signalAll(x) cond_broadcast(x)

/* De-initalize CV and Destroy members */
int cond_destroy(cond_t* cond);