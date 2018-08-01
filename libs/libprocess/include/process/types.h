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
 * @brief Exported type definitions for libprocess
 *
 */

#pragma once

#include <stdbool.h>

#include <sel4/sel4.h>
#include <vka/vka.h>
#include <vspace/vspace.h>
#include <sel4utils/vspace.h>
#include <sel4utils/elf.h>


#include <init/init.h>
#include <thread/thread.h>

/**
 * Possible process states
 */
typedef enum {
    PROCESS_INIT,
    PROCESS_RUNNING,
    PROCESS_DESTROYED
} process_state_t;

/**
 * Possible connection object types.
 */
typedef enum {
    PROCESS_ENDPOINT,
    PROCESS_NOTIFICATION,
    PROCESS_SHARED_MEMORY,
    PROCESS_MAX_NUM_CONN_TYPES
} process_conn_type_t;


/**
 * @brief A generic connection permissions bitfield.
 *
 * Possible connection permissions; not all permissions may apply to a connection:
 * - Endpoints and Notifications ignore the executable permissions.
 * - Shared memory mappings ignore the grant permissions
 */
typedef struct {
    unsigned int r: 1;
    unsigned int w: 1;
    unsigned int x: 1;
    unsigned int g: 1;
} process_conn_perms_t;


/**
 * Attributes for the process_connect call
 */
typedef struct process_conn_attr {
    seL4_Word badge;
} process_conn_attr_t;


/**
 * Attributes for the process_create_conn_obj call
 */
typedef struct process_conn_obj_attr {
    /* Attributes for shmem */
    seL4_Word num_pages;
    seL4_Word page_bits;
} process_conn_obj_attr_t;


/**
 * Connection object fields specific to endpoints and notifications
 */
typedef struct process_ep_conn {
    vka_object_t vka_obj;
} process_ep_conn_t;


/**
 * Connection object fields specific to shmem connections.
 */
typedef struct process_shmem_conn {
    vka_object_t *vka_obj_list;
    seL4_Word page_bits;
    seL4_Word num_pages;

    bool self_mapped;
    reservation_t self_res;
    void *self_addr;
} process_shmem_conn_t;


/**
 * Connection object
 */
typedef struct process_conn_obj {
    process_conn_type_t typ;
    const char *name;
    seL4_Word ref_count;

    /**
     * The type-specific fields are stored in this union.
     * The typ field determines which union field is active.
     */
    union {
        process_ep_conn_t ep;
        process_ep_conn_t notif;
        process_shmem_conn_t shmem;
    } obj;
} process_conn_obj_t;


/**
 * Out parameter for process_connect.
 * This currently is only used to return the address/caps of self connections.
 */
typedef union process_conn_ret {
    void *self_shmem_addr;
    seL4_CPtr self_cap;
} process_conn_ret_t;


/**
 * @brief List node for tracking used connection objects.
 * 
 * When a process is connected to a conn obj, it uses this struct to track that connection.
 * When the process is destroyed the conn obj's reference count is decremented.
 */
typedef struct process_shared_objects_ref {
    struct process_shared_objects_ref *next;
    process_conn_obj_t *ref;
} process_shared_objects_ref_t;


/**
 * A struct used to bookkeep misc objects allocated by vka which need to be freed at destuction.
 */
typedef struct process_object {
    struct process_object *next;
    vka_object_t obj;
} process_object_t;


/**
 * A set of attributes used to create a process
 */
typedef struct process_attr {
    seL4_Word heap_size_pages;
    seL4_Word stack_size_pages;

    seL4_Word priority;
    seL4_Word cpu_affinity;

    seL4_Word cnode_size_bits;

    bool create_fault_ep;
    seL4_CPtr existing_fault_ep;

    bool give_asid_pool;
} process_attr_t;


/**
 * @brief Userspace bookeeping for a child process resources.
 */
typedef struct process_handle {

    process_state_t state;
    const char *name;
    process_attr_t attrs;

    /**
     * Protobuf struct holding the data we will send to the child.
     */
    InitData init_data;

    /**
     * We load the elf at creation time, so we need to track this information for process_run
     */
    void* entry_point;
    int num_elf_phdrs;
    Elf_Phdr *elf_phdrs;
    uintptr_t sysinfo;

    /**
     * This is the location of the heap in the child's address space.
     */
    void* heap_vaddr;

    /**
     * Process specific objects that we create on behalf of the child.
     */
    vka_object_t cnode;
    vka_object_t fault_ep;
    vka_object_t page_dir;
    vka_object_t vspace_lock_notification;
    vka_object_t vka_lock_notification;
    vka_object_t init_data_lock_notification;

    thread_handle_t *main_thread;

    /**
     * Any untypeds given are tracked here. TODO: does freeing this objects kill grandchildren?
     */
    process_object_t *untyped_allocation_list;

    /**
     * A virtual memory manager for the child vspace.
     * This is only valid until the child starts running.
     * When vspace creates page tables they go into the vspace_allocation_list.
     */
    vspace_t vspace;
    sel4utils_alloc_data_t vspace_data;
    process_object_t *vspace_allocation_list;

    /**
     * Details about the cspace layout of the child
     */
    seL4_Word cnode_root_data;
    int cnode_next_free;
    
    /**
     * Bookkeeping for updating connection object reference counts
     */
    process_shared_objects_ref_t *shared_objects;

} process_handle_t;
