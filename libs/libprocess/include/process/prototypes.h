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
 * @param       attr            Optional attributes, use process_default_attrs for config defaults.
 * @param[out]  handle          Handle to initalize
 * @return                      Error code
 */
int process_create(const char *elf_file_name,
                   const char *proc_name,
                   const process_attr_t *attr,
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
int process_run(process_handle_t *handle, int argc, char *argv[]);



/****** Device/DMA/SoC Configuration ******/

/**
 * @brief Map device memory into a process's virtual memory space.
 *
 * @param   handle          Target process vspace
 * @param   paddr           Starting physical memory address of device region to map
 * @param   length_bytes    Size of region to map
 * @param   device_name     Name of the device resource for lookups
 * @return                  Error code
 */
int process_add_device_pages(process_handle_t *handle,
                             void *paddr,
                             seL4_Word num_pages,
                             seL4_Word page_size_bits,
                             const char *device_name);

/**
 * @brief Delegate a device interrupt to a given process.
 *
 * @param   handle      Target process to give irq
 * @param   irq_number  Hardware interrupt number to give
 * @param   device_name Name of the device resource for lookups
 * @return              Error code
 */
int process_add_device_irq(process_handle_t *handle,
                           int irq_number, 
                           const char * device_name);



/****** IPC Configuration ******/

/**
 * @brief Connect two processes with a new endpoint for IPC.
 *
 * @param   handle1     First process to give new ep
 * @param   perms1      Permissions to give first process
 * @param   handle1     Second process to give new ep
 * @param   perms1      Permissions to give Second process
 * @param   conn_name   Name of the connection for lookups
 */
int process_connect_ep(process_handle_t *handle1, seL4_CapRights_t perms1,
                       process_handle_t *handle2, seL4_CapRights_t perms2,
                       const char *conn_name);


/**
 * @brief Connect two processes with a chunk of shared memory.
 *
 * @param   num_pages       Size of the shared memory page to setup.
 * @param   handle1         First process to give new page
 * @param   perms1          Permissions to give first process
 * @param   handle1         Second process to give new page
 * @param   perms1          Permissions to give Second process
 * @param   conn_name       Name of the connection for lookups
 */
int process_connect_shmem(process_handle_t *handle1, seL4_CapRights_t perms1, 
                          process_handle_t *handle2, seL4_CapRights_t perms2,
                          seL4_Word num_pages,
                          const char *conn_name);


/**
 * @brief Connect two processes with a new notification endpoint
 *
 * @param   handle1     First process to give new ep
 * @param   perms1      Permissions to give first process
 * @param   handle1     Second process to give new ep
 * @param   perms1      Permissions to give Second process
 * @param   conn_name   Name of the connection for lookups
 */
int process_connect_notification(process_handle_t *handle1, seL4_CapRights_t perms1,
                                 process_handle_t *handle2, seL4_CapRights_t perms2,
                                 const char *conn_name);


/****** Advanced Configuration ******/

/**
 * @brief Give a process a chunk of untyped memory.
 *
 * Give memory for a process to create it's own kernel objects to start new threads
 * and processes. It can also dynamically allocate memory. To do this, the root task
 * is giving away part of its own untyped memory chunks.
 *
 * @warning Adding untyped memory will allow this process to create new threads and processes.
 * 
 * @param   handle          Target process
 * @param   length_bytes    Size of the untyped chunk to give over.
 * @param   num_objects     Size of the untyped chunk to give over.
 * @return                  Error code
 */
int process_give_untyped_resources(process_handle_t *handle, 
                                   seL4_Word size_bits, 
                                   seL4_Word num_objects);



int process_map_pages_at(process_handle_t *handle,
                         process_mapping_attr_t *attr,
                         seL4_Word num_pages,
                         void *vaddr);

int process_map_pages(process_handle_t *handle,
                      process_mapping_attr_t attr,
                      seL4_Word num_pages,
                      void **vaddr);




/* ~~~ TODO: PHASE 2 API DESIGN ~~~ */
/* Simple defaults */
//int process_create_default(process_handle_t * handle);
//int process_connect_ep_one_way(process_handle_t * handle1, process_handle_t *handle2);
//int process_connect_ep_two_way(process_handle_t * handle1, process_handle_t *handle2);

/* Manual configuration */
//int process_add_thread(process_handle_t* handle ...);
//int process_add_endpoint(process_handle_t *handle, vka_object_t ep);
//int process_add_notification(process_handle_t *handle, vka_object_t ep);
//int process_map_pages(process_handle_t *handle, void *vaddr, seL4_Word num_pages);

/* Sync stuff */
//int process_add_semaphore(...)

/* Debugging, listing */
//void process_print_children(void);

//process kill

