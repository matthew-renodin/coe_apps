/**
 * @file types.h
 * @brief Type definitions for libthread
 */

#pragma once

#include <sel4/sel4.h>

typedef struct thread_caps {
    seL4_CPtr tcb_cap;
    seL4_CPtr ipc_buffer_cap;
} thread_caps_t;


typedef struct thread_handle {
    //int lock;

    seL4_Word thread_id;
    thread_caps_t caps;

    void *stack_vaddr;
    seL4_Word stack_size_pages;
    
} thread_handle_t;
