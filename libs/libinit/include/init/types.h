/**
 * @file types.h
 * @brief Type definitions for libinit
 */
#pragma once

#include <sel4/sel4.h>
#include <simple/simple.h>
#include <allocman/allocman.h>
#include <vka/vka.h>
#include <vspace/vspace.h>

#include <init/init.h>
#include "init_data.pb-c.h"

/**
 * @brief the return value of irq lookups.
 */
typedef struct init_irq_caps {
    seL4_CPtr ep;
    seL4_CPtr irq;
} init_irq_caps_t;


/**
 * @brief Bookkeeping objects/managers/allocators
 *
 * @warning Remeber that these objects are not thread safe. This struct is a 
 * critical resource which must be protected with the lock.
 */
typedef struct init_objects {
    int lock;

    int initialized;

    vspace_t vspace;
    vka_t vka;
    allocman_t *allocman;
    simple_t simple;
    seL4_BootInfo *info;

    /* We can abstract away from boot info here */
    seL4_CPtr cnode_cap;
    seL4_CPtr page_dir_cap;
    seL4_CPtr tcb_cap;
    seL4_CPtr fault_cap;
    seL4_CPtr asid_pool_cap;
    seL4_CPtr asid_control_cap;
    seL4_CPtr sync_notification_cap;

    const char *proc_name;

    InitData *init_data;

} init_objects_t;
