/**
 * @file layouts.h
 * @brief Header file with enumerations and definitions of memory and capability layouts.
 */


#pragma once


/**
 * Child process cnode layout. Additional caps may be allocted for threads.
 */
#define INIT_CHILD_CNODE_SLOT               0
#define INIT_CHILD_PAGE_DIR_SLOT            1
#define INIT_CHILD_FAULT_EP_SLOT            2
#define INIT_CHILD_TCB_SLOT                 3
#define INIT_CHILD_VKA_LOCK_SLOT            4
#define INIT_CHILD_INIT_OBJECTS_LOCK_SLOT   5
#define INIT_CHILD_SYNC_NOTIFICATION_SLOT   6
#define INIT_CHILD_FIRST_FREE_SLOT          7



