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
 * @brief Exported prototype definitions for libprocess
 */

#pragma once


#include <sel4/sel4.h>

#include "types.h"

/**
 * @brief Create a new process handle.
 * 
 * Setup a process handle struct with fresh kernel objects. 
 * Process won't begin until process_run is called.
 *
 * @param       elf_file_name   The name of the executable file to run
 * @param       stack_size      Initial thread's stack space to allocate
 * @param       priority        Initial threads's priority
 * @param       priority        Initial threads's core affinity, ignored for single core systems
 * @param[out]  handle          Handle to initalize
 * @return                      Error code
 */
int process_create(const char *elf_file_name,
                   seL4_Word stack_size,
                   seL4_Word priority,
                   seL4_Word cpu_affinity,
                   process_handle_t *handle);

/**
 * @brief Start executing a new process.
 *
 * Start executing the initial thread in a new process
 * Once this function is called, the process handle should
 * not be modified.
 *
 * @param handle
 */
void process_run(process_handle_t *handle);



/****** Device/DMA/SoC Configuration ******/

/**
 * @brief Map device memory into a process's virtual memory space.
 *
 * @param   handle          Target process vspace
 * @param   paddr           Starting physical memory address of device region to map
 * @param   length_bytes    Size of region to map
 * @return                  Error code
 */
int process_add_device_memory(process_handle_t *handle, void *paddr, seL4_Word length_bytes);

/**
 * @brief Delegate a device interrupt to a given process.
 *
 * @param   handle      Target process to give irq
 * @param   irq_number  Hardware interrupt number to give
 * @return              Error code
 */
int process_add_device_irq(process_handle_t *handle, int irq_number);



/****** IPC Configuration ******/

/**
 * @brief Connect two processes with a new endpoint for IPC.
 *
 * @param   handle1 First process to give new ep
 * @param   perms1  Permissions to give first process
 * @param   handle1 Second process to give new ep
 * @param   perms1  Permissions to give Second process
 */
int process_connect_ep(process_handle_t *handle1, seL4_CapRights_t perms1,
                       process_handle_t *handle2, seL4_CapRights_t perms2);


/**
 * @brief Connect two processes with a chunk of shared memory.
 *
 * @param   length_bytes    Size of the shared memory page to setup.
 * @param   handle1         First process to give new page
 * @param   perms1          Permissions to give first process
 * @param   handle1         Second process to give new page
 * @param   perms1          Permissions to give Second process
 */
int process_connect_shmem(seL4_Word length_bytes,
                          process_handle_t *handle1, seL4_CapRights_t perms1, 
                          process_handle_t *handle2, seL4_CapRights_t perms2);



/****** Thread Configuration ******/

/**
 * @brief Add a worker thread to the new process.
 *
 * Thread is started by the new process.
 * 
 * @param   handle
 * @param   stack_size      Thread's stack space to allocate
 * @param   priority        Threads's priority
 * @param   priority        Threads's core affinity, ignored for single core systems
 * @return                  Error code
 */
int process_add_thread(process_handle_t *handle, 
                       seL4_Word stack_size,
                       seL4_Word priority,
                       seL4_Word cpu_affinity);



/****** Advanced Configuration ******/

/**
 * @brief Give a process a chunk of untyped memory.
 *
 * Giving a process access to untyped memory will allow it to manage its own page tables.
 * This will allow malloc to be backed by virtual memory. This also means that it can use
 * that memory to create it's own kernel objects to start new threads and processes. To
 * do this, the root task is giving away part of its own untyped memory chunks.
 *
 * @warning Adding untyped memory will allow this process to create new threads and processes.
 * 
 * @param   handle          Target process
 * @param   length_bytes    Size of the untyped chunk to give over.
 * @return                  Error code
 */
int process_add_untyped(process_handle_t *handle, seL4_Word length_bytes);


/* TODO: finish api and doc */
//int process_add_endpoint(process_handle_t *handle, vka_object_t ep);
//int process_add_notification(process_handle_t *handle, vka_object_t ep);
//int process_map_page(process_handle_t *handle, void *vaddr);

