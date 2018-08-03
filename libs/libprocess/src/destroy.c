/**
 * @file destroy.c
 * @brief Implemntation of process destruction/cleanup
 *
 */
#define _GNU_SOURCE
#include <autoconf.h>

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <sel4/sel4.h>
#include <vka/capops.h>
#include <utils/util.h>
#include <vspace/vspace.h>
#include <sel4utils/vspace.h>
#include <sel4utils/vspace_internal.h>
#include <sel4utils/helpers.h>

#include <init/init.h>
#include <mmap/mmap.h>
#include <process/process.h>
#include <process/sync.h>
#include <process/internal.h>


int process_destroy(process_handle_t *handle)
{
    libprocess_prologue();
    int error, i;

    libprocess_check_initialized();
    libprocess_check_arg(handle);
    
    libprocess_guard(handle->state == PROCESS_DESTROYED, -6, libprocess_epilogue,
                     "Process has already been destroyed");
    handle->state = PROCESS_DESTROYED;

    error = thread_destroy_free_handle_custom(&handle->main_thread, &handle->vspace);
    ZF_LOGE_IF(error, "Failed to destroy thread");
    
    for(i = 0; i < BIT(handle->attrs.cnode_size_bits); i++) {
        cspacepath_t path;
        path.root = handle->cnode.cptr;
        path.capPtr = i;
        path.capDepth = handle->attrs.cnode_size_bits;
        vka_cnode_revoke(&path); /* TODO how does revoke change this? */
        vka_cnode_delete(&path); /* TODO how does revoke change this? */
    }

    /**
     * Free the heap, code, data
     */
    vspace_tear_down(&handle->vspace, VSPACE_FREE);
    
    /**
     * Free page tables allocated by vspace
     */
    libprocess_free_objects(handle->vspace_allocation_list);
    handle->vspace_allocation_list = NULL;

    /**
     * Free our references to the child's resources.
     */
    vka_free_object(&init_objects.vka, &handle->cnode);
    vka_free_object(&init_objects.vka, &handle->page_dir);
    vka_free_object(&init_objects.vka, &handle->vspace_lock_notification);
    vka_free_object(&init_objects.vka, &handle->vka_lock_notification);
    vka_free_object(&init_objects.vka, &handle->init_data_lock_notification);
    vka_free_object(&init_objects.vka, &handle->process_lock_notification);
    vka_free_object(&init_objects.vka, &handle->thread_lock_notification);

    if(handle->attrs.create_fault_ep && handle->fault_ep.cptr != seL4_CapNull) {
        vka_free_object(&init_objects.vka, &handle->fault_ep);
    }

    /**
     * Free malloced elf metadata
     */
    free(handle->elf_phdrs);

    /**
     * TODO figure out when to free the fault endpoint
     */

    /**
     * Update the conn object reference counts
     */
    while(handle->shared_objects != NULL) {

        process_conn_obj_t *conn_obj = handle->shared_objects->ref;
        if(conn_obj != NULL) {
            conn_obj->ref_count--;
        } else { 
            ZF_LOGE("Invalid conn obj");
        }
        
        process_shared_objects_ref_t *tmp = handle->shared_objects;
        handle->shared_objects = handle->shared_objects->next;
        free(tmp);
    }

    /**
     * Free the device objects
     */
    libprocess_free_objects(handle->device_allocation_list);

    /**
     * Free all the untypeds
     */
    libprocess_revoke_objects(handle->untyped_allocation_list);
    handle->untyped_allocation_list = NULL;

    /* Free the strduped name */
    free(handle->name);
    
    libprocess_return_success();
    libprocess_epilogue();
}
