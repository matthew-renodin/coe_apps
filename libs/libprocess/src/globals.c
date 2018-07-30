/**
 * @file globals.c
 * @brief Global variable definitions
 *
 */

#include <autoconf.h>
#include <sel4/sel4.h>
#include <process/process.h>

const process_attr_t process_default_attrs = {
    .heap_size_pages    = CONFIG_LIB_PROCESS_DEFAULT_HEAP_SIZE_PAGES,
    .stack_size_pages   = CONFIG_LIB_PROCESS_DEFAULT_STACK_SIZE_PAGES,
    .priority           = CONFIG_LIB_PROCESS_DEFAULT_PRIORITY,
    .cpu_affinity       = CONFIG_LIB_PROCESS_DEFAULT_CPU_AFFINITY,
    .cnode_size_bits    = CONFIG_LIB_PROCESS_DEFAULT_CNODE_SIZE_BITS,
    .create_fault_ep    = 0,
    .existing_fault_ep  = seL4_CapNull,
};

const process_conn_perms_t process_rw = {.r=1, .w=1, .x=0, .g=0};
const process_conn_perms_t process_rx = {.r=1, .w=0, .x=1, .g=0};
const process_conn_perms_t process_rwg = {.r=1, .w=1, .x=0, .g=1};
const process_conn_perms_t process_ro = {.r=1, .w=0, .x=0, .g=0};

const process_conn_obj_attr_t process_default_shmem_4k = {
    .num_pages = 1,
    .page_bits = PAGE_BITS_4K,
};
