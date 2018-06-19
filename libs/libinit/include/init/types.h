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

} init_objects_t;
