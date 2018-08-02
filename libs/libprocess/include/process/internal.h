/**
 * @file internal.h
 * @brief Internal definitions for libprocess
 */

#pragma once


#include <sel4/sel4.h>

#include "types.h"

/* Definition of generic linked list operations */
#define LINKED_LIST_PREPEND(object, head) do { \
    object->next = head; \
    head = object; \
} while(0)

#define LINKED_LIST_POP(object, head) do {\
    object = head; \
    head = object->next; \
} while(0)

/**
 * Definition for function pointer type.
 * Generic copy cap into process.
 */
typedef int (copy_cap_to_proc_func_t)(process_handle_t *handle,
                                      seL4_CPtr cap_to_copy,
                                      seL4_CapRights_t perms,
                                      const char* conn_name);

seL4_CPtr libprocess_copy_cap_next_slot(process_handle_t *handle,
                                        seL4_CPtr new_cap,
                                        seL4_CapRights_t perms);

seL4_CPtr libprocess_mint_cap_next_slot(process_handle_t *handle,
                                        seL4_CPtr new_cap,
                                        seL4_CapRights_t perms,
                                        seL4_Word badge);

int libprocess_delete_cap_last_slot(process_handle_t *handle);

void libprocess_next_free_path(cspacepath_t *dst, process_handle_t *handle);


void libprocess_free_objects(process_object_t *list);
void libprocess_revoke_objects(process_object_t *list);
