/*
 * types.h
 *
 *
 * Exported type definitions for libprocess
 */

#pragma once


/******************************************************************************
 * INCLUDES
 *****************************************************************************/
#include <sel4/sel4.h>

/******************************************************************************
 * TYPES
 *****************************************************************************/

typedef struct process_caps {
    seL4_CPtr tcb_cap;
    seL4_CPtr cnode_cap;
    seL4_CPtr page_dir_cap;
    seL4_CPtr fault_ep_cap;
    seL4_CPtr ipc_buffer_cap;
    //seL4_CPtr untyped_resources_cap;
} process_caps_t;

typedef struct process_handle {
    /* Local caps to a child process */
    process_caps_t local;

    /* Child processes' copy of it caps. */ 
    process_caps_t remote;
} process_handle_t;

