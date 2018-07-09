/**
 * @file globals.c
 * @brief Global variable definitions
 *
 */

#include <autoconf.h>
#include <process/process.h>

const process_attr_t process_default_attrs = {
    .heap_size_pages    = CONFIG_LIB_PROCESS_DEFAULT_HEAP_SIZE_PAGES,
    .stack_size_pages   = CONFIG_LIB_PROCESS_DEFAULT_STACK_SIZE_PAGES,
    .priority           = CONFIG_LIB_PROCESS_DEFAULT_PRIORITY,
    .cpu_affinity       = CONFIG_LIB_PROCESS_DEFAULT_CPU_AFFINITY,
    .cnode_size_bits    = CONFIG_LIB_PROCESS_DEFAULT_CNODE_SIZE_BITS,
};
