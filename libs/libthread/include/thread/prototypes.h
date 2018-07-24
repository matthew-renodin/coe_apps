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
 * @file prototypes.h
 * @brief Exported prototype definitions for libthread
 *
 */

#pragma once

#include <sel4/sel4.h>
#include <vka/vka.h>
#include <vspace/vspace.h>

#include "types.h"


/**
 * @brief Create a new thread handle and populate it with new thread objects
 *
 * @param       stack_size_pages    Number of pages to map for the stack.
 * @param       priority            New thread's priority
 * @param       cpu_affinity        New thread's CPU affinity
 * @param[out]  handle              Handle to initialize
 * @return                          Error code, TODO: thread id?
 */
thread_handle_t *thread_handle_create(const thread_attr_t *attrs);


/**
 * @brief Start a thread's execution.
 *
 * @param   handle          Target thread handle
 * @param   start_routine   Function to begin execution
 * @param   arg             Argument to pass to new thread
 * @return                  Error code
 */
int thread_start(thread_handle_t *handle, void *(*start_routine) (void *), void *arg);


/**
 * @brief configure a thread with a string name.
 *
 * @param   handle  Target thread handle
 * @param   name    New thread name
 */
void thread_set_name(thread_handle_t *handle, const char *name);


/**
 * @brief Get the id of the currently executing thread.
 *
 * @return The ID
 */
int thread_get_id();

/**
 * @brief Get the sync notification of the currently executing thread.
 *
 * @return The cap to the notification
 */
seL4_CPtr thread_get_sync_notification();

/**
 * @brief Set the thread-specific data for the currently executing thread.
 *
 * This is expected to be typed as thread_handle_t.
 */
int thread_set_current_local_storage(thread_handle_t *handle);

/**
 * @brief Get the thread-specific data for the currently executing thread.
 *
 * This is expected to be typed as thread_handle_t.
 */
thread_handle_t *thread_get_current_local_storage();

/**
 * @brief Get the handle for the current thread
 *
 * @return The handle
 */
thread_handle_t * thread_handle_get_current();


/**
 * @brief Wait until a worker thread is finished executing.
 *
 * @return the worker thread's return value.
 */
void *thread_join(thread_handle_t *handle);


thread_handle_t *thread_handle_create_custom(seL4_CPtr cnode,
                                             seL4_CPtr cnode_root_data,
                                             seL4_CPtr fault_ep,
                                             seL4_CPtr page_dir,
                                             vspace_t *vspace,
                                             const thread_attr_t *attr);


int thread_destroy_free_handle(thread_handle_t **handle);

int thread_destroy_free_handle_custom(thread_handle_t **handle,
                                      vspace_t *vspace);

/* ~~~ TODO: API PHASE 2 ~~~ */
/* debugging, listing */
//void thread_print_threads(void);

