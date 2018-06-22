/**
 * @file layouts.h
 * @brief Header file with enumerations and definitions of memory and capability layouts.
 */


#pragma once


/**
 * Child process cnode layout. Additional caps may be allocted for threads.
 */
#define INIT_CHILD_CNODE_SLOT           0
#define INIT_CHILD_PAGE_DIR_SLOT        1
#define INIT_CHILD_FAULT_EP_SLOT        2
#define INIT_CHILD_TCB_SLOT             3
#define INIT_CHILD_FIRST_FREE_SLOT      4





/**
 * Child process memory layout. TODO: this is 100% arbitrary and not ideal, fix
 * We can maybe use the env struct passed into main
 */
#define INIT_CHILD_HEAP_ADDR            ((void *)0x70000000)
#define INIT_CHILD_INIT_DATA_ADDR       ((void *)0x60000000)
