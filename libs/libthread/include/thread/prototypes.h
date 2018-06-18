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
int thread_handle_create(seL4_Word stack_size_pages,
                         seL4_Word priority,
                         seL4_Word cpu_affinity,
                         thread_handle_t *handle);


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
seL4_Word thread_get_id();


/**
 * @brief Get a thread handle given a thread ID.
 *
 * @return The handle
 */
thread_handle_t * thread_handle_get(seL4_Word id);



/* ~~~ TODO: API PHASE 2 ~~~ */

/* simple defaults */
//int thread_handle_create_default(thread_handle_t *handle);

/* debugging, listing */
//void thread_print_threads(void);
