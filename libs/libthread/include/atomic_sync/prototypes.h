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

/**
 * @brief Create a new thread mutex lock and populate its objects
 *
 * This will create a mutex, populating it with the necessary
 * data structures. For the notification-based variants, the
 * mutex will use the init_data vka to allocate the notification
 * endpoints. If mutex_destroy is called on mutexes created using
 * this call, it will use vka_free to deallocate objects
 * 
 * @param[out]  mutex               Lock to initialize
 * @param       type                Requested type of lock
 * @return                          Error code
 */
int mutex_create(mutex_t *mutex, lock_type_t type);

/**
 * @brief Create a new userspace mutex lock
 * 
 * @param[out]  mutex               Lock to initialize
 * @param       recursive           Whether the lock is recursive (re-entrant)
 * @return                          Error code
 */
int mutex_spinlock_init(mutex_t *mutex, bool recursive);

/**
 * @brief Create a new notification-based mutex lock
 *
 * This will create a notification-based mutex. Note that the cptr
 * provided here will not be de-allocated by the mutex_destroy
 * function.
 * 
 * @param[out]  mutex               Lock to initialize
 * @param       notification        seL4_CPtr of the notification the lock uses
 * @param       recursive           Whether the lock is recursive (re-entrant)
 * @return                          Error code
 */
int mutex_notification_init(mutex_t *mutex, seL4_CPtr notification, bool recursive);

/**
 * @brief Acquire the mutex lock
 *
 * Note that deadlock will occur if a thread holding 
 * a non-recursive lock tries to call lock again.
 * 
 * @param       mutex               Lock to acquire
 * @return                          Error code
 */
int mutex_lock(mutex_t *mutex);

/**
 * @brief Acquire the mutex lock
 *
 * Note that behavior is undefined if a thread 
 * not holding the lock tries to call unlock and varies
 * based on lock type. 
 * 
 * @param       mutex               Lock to release
 * @return                          Error code
 */
int mutex_unlock(mutex_t *mutex);

/**
 * @brief Destroy the mutex lock
 *
 * If the lock was created using mutex create, all vka-allocated
 * objects will be freed. Otherwise, objects provided to the lock
 * initialization function will not be modified.
 * 
 * @param       mutex               Lock to destroy
 * @return                          Error code
 */
int mutex_destroy(mutex_t *mutex);

/**
 * @brief Create a new condition variable and populate its objects
 *
 * This will create a condition variable, populating it with the necessary
 * data structures (including a new mutex of type lock_type). 
 * For the notification-based variants, the mutex will use the init_data vka 
 * to allocate the notification endpoints. Do not modify the lock directly, as
 * it is managed by the condition variable
 * 
 * @param[out]  cond                Condition Variable to initialize
 * @param       type                Requested type of lock
 * @return                          Error code
 */
int cond_init(cond_t *cond, lock_type_t lock_type);

/**
 * @brief Initialize a new condition variable using an existing lock
 *
 * This will attach a condition variable to an existing mutex. Note that
 * the mutex provided here will not be de-allocated by the cond_destroy
 * function.
 * 
 * @param[out]  cond                Condition Variable to initialize
 * @param       mutex               Lock for cond to attach to
 * @return                          Error code
 */
int cond_attach(cond_t *cond, mutex_t *lock);

/**
 * @brief Acquire the mutex lock
 *
 * This should be used preferentially over accessing the CV fields directly
 * 
 * @param       cond                Condition Variable containing the lock to acquire
 * @return                          Error code
 */
int cond_lock_acquire(cond_t *cond);

/**
 * @brief Release the mutex lock
 *
 * This should be used preferentially over accessing the CV fields directly
 * 
 * @param       cond                Condition Variable containing the lock to release
 * @return                          Error code
 */
int cond_lock_release(cond_t *cond);

/**
 * @brief Wait on this condition variable
 *
 * Precondition: Caller holds main_lock.
 * This requires the init() call to have been run from libinit, as
 * it uses the lock notification that is set by that library
 * 
 * @param       cond                Condition Variable
 * @return                          Error code
 */
int cond_wait(cond_t *cond); 

/**
 * @brief Signal this condition variable
 *
 * If any threads are waiting on this condition variable,
 * one of them will be awoken.
 * 
 * @param       cond                Condition Variable
 * @return                          Error code
 */
int cond_signal(cond_t *cond);

/**
 * @brief Broadcase on this condition variable
 *
 * If any threads are waiting on this condition variable,
 * all of them will be awoken.
 * 
 * @param       cond                Condition Variable
 * @return                          Error code
 */
int cond_broadcast(cond_t *cond);
#define cond_signalAll(x) cond_broadcast(x)

/**
 * @brief Destroy the condition variable
 *
 * If the CV was created using cond_create, all vka-allocated
 * objects will be freed. Otherwise, objects provided to the CV
 * initialization function will not be modified.
 * 
 * @param       cond                CV to destroy
 * @return                          Error code
 */
int cond_destroy(cond_t* cond);
