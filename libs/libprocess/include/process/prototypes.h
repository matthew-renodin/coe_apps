/*
 * prototypes.h
 *
 *
 * Exported prototype definitions for libprocess
 */

#pragma once


/******************************************************************************
 * INCLUDES
 *****************************************************************************/
#include <sel4/sel4.h>

#include "types.h"


/******************************************************************************
 * PROTOTYPES
 *****************************************************************************/
int process_create(const char *elf_file, seL4_Word stack_size, process_handle_t *handle);

//int process_add_untyped(process_handle_t *handle); //pending solution to tcb creation/malloc 

/* Device/DMA/SoC Configuration */
int process_add_device_memory(process_handle_t *handle, void *paddr, seL4_Word length_bytes);
int process_add_device_irq(process_handle_t *handle, int irq_number);

/* IPC Configuration */
int process_connect_ep(process_handle_t *handle1, seL4_CapRights_t perms1,
                       process_handle_t *handle2, seL4_CapRights_t perms2);
int process_connect_shmem(seL4_Word length_bytes,
                          process_handle_t *handle1, seL4_CapRights_t perms1, 
                          process_handle_t *handle2, seL4_CapRights_t perms2);





