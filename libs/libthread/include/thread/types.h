/**
 * @file types.h
 * @brief Type definitions for libthread
 */

#pragma once

#include <sel4/sel4.h>
#include <vka/vka.h>


typedef struct thread_attr {
    seL4_Word stack_size_pages;
    seL4_Word priority;
    seL4_Word cpu_affinity;
} thread_attr_t;


typedef struct thread_handle {
    //int lock;

    seL4_Word thread_id;

    vka_object_t tcb;
    vka_object_t sync_notification;
    vka_object_t join_ep;

    void *stack_vaddr;
    seL4_Word stack_size_pages;

    void *ipc_buffer_vaddr;
    seL4_CPtr ipc_buffer_cap;
    
} thread_handle_t;
