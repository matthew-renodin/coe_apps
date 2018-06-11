/**
 * @file types.h
 * @brief Exported type definitions for libprocess
 *
 */

#pragma once


#include <sel4/sel4.h>

/**
 * @brief Collect all the capabilities to a process's resources.
 */
typedef struct process_caps {
    seL4_CPtr tcb_cap;
    seL4_CPtr cnode_cap;
    seL4_CPtr page_dir_cap;
    seL4_CPtr fault_ep_cap;
    seL4_CPtr ipc_buffer_cap;
    //seL4_CPtr untyped_resources_cap;
} process_caps_t;


/**
 * @brief Userspace bookeeping for a child process resources.
 */
typedef struct process_handle {
    /* Only one thread can modify this structure at once */
    int lock;
    int running;

    /* Local caps to a child process */
    process_caps_t local;

    /* Child processes' copy of it caps. */ 
    process_caps_t remote;
} process_handle_t;

