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
#include <sync/mutex.h>
#include <sync/recursive_mutex.h>

#include <lockwrapper/lockwrapper.h>

#include <init/init.h>
#include "init_data.pb-c.h"

/**
 * @brief the return value of irq lookups.
 */
typedef struct init_irq_info {
    seL4_CPtr ep;
    seL4_CPtr irq;
    seL4_Word number;
} init_irq_info_t;

/**
 * @brief the return value of device memory lookups.
 */
typedef struct init_devmem_info {
    void *vaddr;
    void *paddr;
    seL4_Word size_bits;
    seL4_Word num_pages;
    seL4_CPtr *caps;
} init_devmem_info_t;

/**
 * @brief Bookkeeping objects/managers/allocators
 *
 * @warning Remeber that these objects are not thread safe. This struct is a 
 * critical resource which must be protected with the lock.
 */
typedef struct init_objects {
    sync_recursive_mutex_t init_lock;

    int initialized;

    vspace_t vspace;
    vka_t vka;
    allocman_t *allocman;
    simple_t simple;
    seL4_BootInfo *info;

    sync_recursive_mutex_t vspace_lock;
    lockvspace_t lockvspace;

    sync_mutex_t vka_lock;
    lockvka_t lockvka;

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
