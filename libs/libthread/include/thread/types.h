/**
 * @file types.h
 * @brief Type definitions for libthread
 */

#pragma once

#include <sel4/sel4.h>
#include <vka/vka.h>


typedef struct thread_handle {
    //int lock;

    seL4_Word thread_id;

    vka_object_t tcb;

    void *stack_vaddr;
    seL4_Word stack_size_pages;
    
} thread_handle_t;
