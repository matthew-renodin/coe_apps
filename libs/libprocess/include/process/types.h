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
 *
 */
typedef struct process_attr {
    seL4_Word heap_size_pages;
    seL4_Word stack_size_pages;

    seL4_Word priority;
    seL4_Word cpu_affinity;

    seL4_Word cnode_size_bits;

    bool create_fault_ep;
    seL4_CPtr existing_fault_ep;
} process_attr_t;



typedef struct process_shared_objects {
    seL4_Word ref_count;
    vka_object_t *obj_list;
    seL4_Word num_objs;
} process_shared_objects_t;


typedef struct process_shared_objects_ref {
    struct process_shared_objects_ref *next;
    process_shared_objects_t *ref;    
} process_shared_objects_ref_t;


/**
 *
 */
typedef struct process_object {
    struct process_object *next;
    vka_object_t obj;
} process_object_t;


typedef enum {
    PROCESS_INIT,
    PROCESS_RUNNING,
    PROCESS_DESTROYED
} process_state_t;



/**
 * @brief Userspace bookeeping for a child process resources.
 */
typedef struct process_handle {
    /* Only one thread can modify this structure at once */
    sync_recursive_mutex_t process_handle_lock;

    process_state_t state;

    const char *name;

    void* entry_point;
    int num_elf_phdrs;
    Elf_Phdr *elf_phdrs;
    uintptr_t sysinfo;

    process_attr_t attrs;

    void* heap_vaddr;
    reservation_t heap_res;

    vka_object_t cnode;
    vka_object_t fault_ep;
    vka_object_t page_dir;
    vka_object_t vspace_lock_notification;
    vka_object_t vka_lock_notification;
    vka_object_t init_data_lock_notification;
    process_object_t *untyped_allocation_list;

    vspace_t vspace;
    sel4utils_alloc_data_t vspace_data;
    process_object_t *vspace_allocation_list;

    seL4_Word cnode_root_data;
    int cnode_next_free;
    
    process_shared_objects_ref_t *shared_objects;

    thread_handle_t *main_thread;

    InitData init_data;

} process_handle_t;


typedef enum {
    PROCESS_ENDPOINT,
    PROCESS_NOTIFICATION,
    PROCESS_SHARED_MEMORY
} process_connect_medium_t;

typedef struct process_connection {
    process_handle_t *handle;
    seL4_CapRights_t perms;
} process_connection_t;


typedef struct process_connect_attrs {
    const char *name;

    process_connection_t *procs;
    seL4_Word num_procs;

    process_connect_medium_t type;

    /* Optional, only used for shmem */
    seL4_Word num_shmem_pages;

    /* Optional, use an existing ep/notification instead of a new one */
    seL4_CPtr existing_cap;

} process_connect_attrs_t;

typedef struct process_connect_result_t {
    /* */
    seL4_CPtr parent_cap;
    void *shmem_addr;
}
