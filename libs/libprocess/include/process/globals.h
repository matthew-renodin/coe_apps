/**
 * @file globals.h
 * @brief Global variables for libprocess
 */
#pragma once

#include "types.h"

#define PROCESS_SELF ((process_handle_t*)NULL)

extern const process_attr_t process_default_attrs;

extern const process_conn_perms_t process_rw; 
extern const process_conn_perms_t process_rx;
extern const process_conn_perms_t process_rwg;
extern const process_conn_perms_t process_ro;

extern const process_conn_obj_attr_t process_default_shmem_4k;
