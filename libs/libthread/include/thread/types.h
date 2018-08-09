/**
 * @file types.h
 * @brief Type definitions for libthread
 */

#pragma once

#include <sel4/sel4.h>
#include <vka/vka.h>
#include <atomic_sync/sync.h>


typedef struct thread_attr {
    seL4_Word stack_size_pages;
    seL4_Word priority;
    seL4_Word max_priority;
    int cpu_affinity;
} thread_attr_t;

typedef enum {
    THREAD_INIT = 0,
    THREAD_RUNNING = 1,
    THREAD_DESTROYED = 2
} thread_state_t;

typedef struct thread_handle {
    //int lock;

    int thread_id;
    thread_state_t state;

    vka_object_t tcb;
    vka_object_t sync_notification;
    vka_object_t join_notification;
    bool join_condition_initialized;
    cond_t join_condition;
    
    void *returned_value;
    
    void *stack_vaddr;
    seL4_Word stack_size_pages;
    reservation_t stack_res;

    void *ipc_buffer_vaddr;
    seL4_CPtr ipc_buffer_cap;
    reservation_t ipc_buffer_res;
    
} thread_handle_t;
