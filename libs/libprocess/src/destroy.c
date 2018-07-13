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

static void free_parent_cap(seL4_CPtr cap)
{
    if(cap == seL4_CapNull) {
        ZF_LOGE("Cannot free null cap");
        return;
    }
    
    cspacepath_t path;
    vka_cspace_make_path(&init_objects.vka, cap, &path);
    seL4_CNode_Delete(path.root, path.capPtr, path.capDepth);

    vka_cspace_free(&init_objects.vka, cap);
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

    /**
     * Free our references to the child's resources.
     */


    vka_free_object(&init_objects.vka, &handle->vka_lock_notification);
    vka_free_object(&init_objects.vka, &handle->init_data_lock_notification);

    //vka_free_object(&init_objects.vka, &handle->cnode);
}
