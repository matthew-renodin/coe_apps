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



static void free_process_objects(process_object_t* list) {
    while(list != NULL) {
        vka_free_object(&init_objects.vka, &list->obj);

        process_object_t *temp = list;
        list = list->next;
        free(temp);
    }
}


int process_destroy(process_handle_t *handle)
{
     int error;

    /* TODO: Implement sync for init objects */
    if(!init_objects.initialized) {
        ZF_LOGW("Init objects (vka, vspace) have not been setup.\n"
                "Run init_process or init_root_task to complete.");
        return -1;
    }

    if(handle == NULL) {
        ZF_LOGE("Null process handle passed");
        return -2; /* TODO come up with error codes */
    }

    if(handle->state == PROCESS_DESTROYED) {
        ZF_LOGE("Process has already been destroyed");
        return -3;
    }
    handle->state = PROCESS_DESTROYED;

    error = thread_destroy_free_handle_custom(&handle->main_thread, &handle->vspace);
    ZF_LOGE_IF(error, "Failed to destroy thread");
    
    for(int i = 0; i < BIT(handle->attrs.cnode_size_bits); i++) {
        cspacepath_t path;
        path.root = handle->cnode.cptr;
        path.capPtr = i;
        path.capDepth = handle->attrs.cnode_size_bits;
        vka_cnode_delete(&path); /* TODO how does revoke change this? */
    }

    /**
     * Free the heap, code, data
     * TODO: figure out how this affects shared memory among many 
     */
    vspace_tear_down(&handle->vspace, VSPACE_FREE);
    
    /**
     * Free page tables allocated by vspace
     */
    free_process_objects(handle->vspace_allocation_list);
    handle->vspace_allocation_list = NULL;

    /**
     * Free our references to the child's resources.
     */
    vka_free_object(&init_objects.vka, &handle->cnode);
    vka_free_object(&init_objects.vka, &handle->page_dir);
    vka_free_object(&init_objects.vka, &handle->vspace_lock_notification);
    vka_free_object(&init_objects.vka, &handle->vka_lock_notification);
    vka_free_object(&init_objects.vka, &handle->init_data_lock_notification);

    /**
     * TODO figure out when to free the fault endpoint
     */


    /**
     * TODO: We leave endpoints and notifications alone for now.
     * This is ultimately a memory leak, but it's pretty small overall.
     */
    

    /**
     * Free all the untypeds
     */
    free_process_objects(handle->untyped_allocation_list);
    handle->untyped_allocation_list = NULL;
    

    return 0;

}