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
 * @file prototype.h
 * @brief Exported prototype definitions for libinit
 *
 */

#pragma once

/**
 * @brief Initializes the necessary userspace bookeeping for a single process.
 */
int init_process(void);

/**
 * @brief Initializes the necesarry bookeeping for the root task using seL4_BootInfo.
 */
int init_root_task(void);


/**
 * @brief Get the fault ep from the init data
 *
 * @return Capability slot of the fault ep.
 */
seL4_CPtr init_get_fault_ep();


/**
 * @breif Lookup an endpoint with a given string name
 *
 * @return Capability slot of the requested ep.
 */
seL4_CPtr init_lookup_ep(const char *);


/**
 * @breif Lookup a notification endpoint with a given string name
 *
 * @return Capability slot of the requested ep.
 */
seL4_CPtr init_lookup_notification(const char *);

/**
 * @brief Lookup shared memory with a given string name
 *
 * @return A pointer to the shared memory region.
 */
void * init_lookup_shmem(const char *);


int init_set_thread_local_storage(void * storage);
void *init_get_thread_local_storage(void);


/**
 * @brief Lookup an IRQ notification ep that was given to us with a string name.
 *
 * This searches the init data for IRQs that our parent process gave to us.
 * Don't use this in the root task.
 *
 * @return Capability to the notification ep.
 */
seL4_CPtr init_lookup_irq(const char *);


/**
 * @brief Lookup a given device memory with a given string name
 *
 * This searches the init data for device mappings that our parent process gave to us.
 * Don't use this in the root task.
 *
 * @return A pointer to the shared memory region.
 */
void * init_lookup_device_addr(const char *);







/* ~~~ TODO: PHASE 2 API DESIGN ~~~ */

//  init_list_devices(...)
//  init_list_connections(...)

