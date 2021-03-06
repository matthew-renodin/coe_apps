/*
 * Copyright 2018, Intelligent Automation, Inc.
 * This software was developed in part under Air Force contract number FA8750-15-C-0066 and DARPA 
 *  contract number 140D6318C0001.
 * This software was released under DARPA, public release number 1.0.
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
 * Start executing the initial thread in a new process.
 * After this function is called, the process cannot be modified.
 *
 * @param handle    The target process
 * @param argc      The number of args to pass
 * @param argv      The list of args to pass
 * @return          Error code
 */
int process_run(process_handle_t *handle, int argc, char *argv[]);


/**
 * @brief destory a process and cleanup its resources.
 *
 * @param handle    The target process
 * @return          Error code
 */
int process_destroy(process_handle_t *handle);


/****** Interprocess Connections/Communication ******/


/**
 * @brief Create a new connection object.
 *
 * This call mallocs a new conn obj struct and initializes it for you.
 * It also will allocate any kernel objects needed to create the object.
 *
 * @param       typ    The type of conn object to create 
 * @param       name   The name of the conn object
 * @param       attr   An object attributes, if null, default attributes are used
 * @param[out]  obj    A double pointer indicating where to store the conn object
 * @return             Error code
 */
int process_create_conn_obj(process_conn_type_t typ,
                            const char *name,
                            const process_conn_obj_attr_t *attr,
                            process_conn_obj_t **obj);

/**
 * @brief Destroy an object and free any associated memory.
 *
 * @warning The object will not be destroyed if any connected processes have
 *          not been destoryed.
 *
 * This call destroys the object, and it will free the struct. The obj param
 * will be set to null.
 *
 * @param   obj     The obj to free.
 * @return          Error code
 */
int process_free_conn_obj(process_conn_obj_t **obj);


/**
 * @brief Connect a process to a connection object. Use PROCESS_SELF to connect to self.
 *
 * @param       handle  The target process
 * @param       obj     The target object
 * @param       perms   The perms to give the newly created capability or mem mapping
 * @param[out]  ret     Optional return parameter, this is only used to return self mapping info
 */
int process_connect(process_handle_t *handle,
                    process_conn_obj_t *obj,
                    process_conn_perms_t perms,
                    process_conn_attr_t *attr,
                    process_conn_ret_t *ret);


/****** Untyped Memory Configuration ******/


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



/****** Device/DMA/SoC Configuration ******/


/**
 * @brief Map device memory into a process's virtual memory space.
 *
 * @param   handle          Target process vspace
 * @param   paddr           Starting physical memory address of device region to map
 * @param   num_pages       Number of pages in the region
 * @param   page_bits       Page size in bits
 * @param   device_name     Name of the device resource for lookups
 * @return                  Error code
 */
int process_map_device_pages(process_handle_t *handle,
                             void *paddr,
                             seL4_Word num_pages,
                             seL4_Word page_bits,
                             const char *device_name);


/**
 * @brief Map device memory into a process's virtual memory space, and copy page
 *        caps to the child. Giving a process caps to its device pages allows a
 *        process to map its device to grandchild processes (or other procs over IPC)
 *
 * @warning Giving caps could allow the child to remap the pages to be executable and writable.
 *
 * @param   handle          Target process vspace
 * @param   paddr           Starting physical memory address of device region to map
 * @param   num_pages       Number of pages in the region
 * @param   page_bits       Page size in bits
 * @param   device_name     Name of the device resource for lookups
 * @return                  Error code
 */
int process_map_device_pages_give_caps(process_handle_t *handle,
                                       void *paddr,
                                       seL4_Word num_pages,
                                       seL4_Word page_bits,
                                       const char *device_name);


/**
 * @brief Map device memory into a process's virtual memory space, given the string name
 *        of that device. The name must be given by the current process's parent. 
 *        That parent must have given the current process caps the device memory.
 *
 * @param   handle          Target process vspace
 * @param   device_name     The name of the current (parent) process's device to give.
 * @param   new_device_name The new name to give the device for lookups.
 */
int process_map_my_device(process_handle_t *handle,
                          const char *device_name,
                          const char *new_device_name);


/**
 * @brief Map device memory into a process's virtual memory space, given the string name
 *        of that device. The name must be given by the current process's parent. 
 *        That parent must have given the current process caps the device memory.
 *        In addition this function will copy the caps to the child process. 
 *        Giving a process caps to its device pages allows a process to map its
 *        device to grandchild processes (or other procs over IPC)
 *
 * @warning Giving caps could allow the child to remap the pages to be executable and writable.
 *
 * @param   handle          Target process vspace
 * @param   device_name     The name of the current (parent) process's device to give.
 * @param   new_device_name The new name to give the device for lookups.
 */
int process_map_my_device_give_caps(process_handle_t *handle,
                                    const char *device_name,
                                    const char *new_device_name);


/**
 * @brief Delegate a device interrupt to a given process.
 *
 * This function creates a notification ep and binds the IRQ to it.
 * It then copies the caps into the child process.
 *
 * @param   handle      Target process to give irq
 * @param   irq_number  Hardware interrupt number to give
 * @param   device_name Name of the device resource for lookups
 * @return              Error code
 */
int process_add_device_irq(process_handle_t *handle,
                           int irq_number, 
                           const char * device_name);


/**
 * @brief Delegate a device interrupt to a given process given the string name of device irq
 *        given by the current process's parent.
 *
 * This function copies the caps for our IRQ and IRQ notification ep to a target process.
 *
 * @param   handle      Target process to give irq
 * @param   irq_number  Hardware interrupt number to give
 * @param   device_name Name of the device resource for lookups
 * @return              Error code
 */
int process_add_my_device_irq(process_handle_t *handle,
                              const char * device_name, 
                              const char * new_device_name);


