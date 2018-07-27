/*
 * Copyright 2018, Intelligent Automation, Inc.
 * This software was developed in part under Air Force contract number FA8750-15-C-0066 and DARPA 
 *  contract number 140D6318C0001.
 * This software was released under DARPA, public release number XXXXXXXX (to be updated once approved)
 * This software may be distributed and modified according to the terms of the BSD 2-Clause license.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND
 * FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY
 * WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * See "LICENSE_BSD2.txt" for details.
 *
 * @TAG(IAI_BSD)
 */
/**
 * @file config.c
 * @brief Implementation of process configuration functions
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
#include <sel4utils/elf.h>
#include <sel4utils/helpers.h>

#include <init/init.h>
#include <mmap/mmap.h>
#include <process/process.h>
#include <process/sync.h>
#include <process/internal.h>



/**
 * Convenience function for copying ep and notifications to process
 * Assumes all arguments have been error checked
 * This function must acquire process lock because current_list_head is a critical resource
 */
static inline int copy_cptr_to_proc(process_handle_t *handle,
                                    seL4_CPtr ep_cap,
                                    seL4_CapRights_t perms,
                                    const char *conn_name,
                                    EndpointData **current_list_head) 
{
    libprocess_prologue();

    EndpointData *ep_data = malloc(sizeof(EndpointData));
    libprocess_guard(ep_data == NULL, -1, libprocess_epilogue,
                     "Failed to malloc Endpoint Data");

    endpoint_data__init(ep_data);
    ep_data->name = (char *)conn_name; /* protobuf uses non const strings */
    ep_data->cap = libprocess_copy_cap_next_slot(handle, ep_cap, perms);
    libprocess_guard(ep_data->cap == seL4_CapNull, -2, free_endpoint,
                     "Failed to copy ep cap");

    LINKED_LIST_PREPEND(ep_data, *current_list_head);
    libprocess_return_success();
    free_endpoint:
        free(ep_data);
    libprocess_epilogue();
}
/**
 * Assumes you have error checked args
 */
static int copy_ep_to_proc(process_handle_t *handle,
                           seL4_CPtr ep_cap,
                           seL4_CapRights_t perms,
                           const char *conn_name) 
{
    return copy_cptr_to_proc(handle, ep_cap, perms, conn_name,
                             &handle->init_data.ep_list_head);
}


/**
 * Assumes you have error checked args
 */
static int copy_notification_to_proc(process_handle_t *handle,
                                     seL4_CPtr ep_cap,
                                     seL4_CapRights_t perms,
                                     const char *conn_name) 
{
    return copy_cptr_to_proc(handle, ep_cap, perms, conn_name,
                             &handle->init_data.notification_list_head);
}


/**
 * Assumes you have error checked args
 */
static int copy_shmem_to_proc(process_handle_t *handle,
                              void *vaddr,
                              seL4_Word num_pages,
                              const char *conn_name) 
{
    libprocess_prologue();

    SharedMemoryData *shmem_data = malloc(sizeof(SharedMemoryData));
    libprocess_guard(shmem_data == NULL, -1, libprocess_epilogue,
                     "Failed to malloc Shmem Data");

    shared_memory_data__init(shmem_data);
    shmem_data->name = (char *)conn_name; /* protobuf uses non const strings */
    shmem_data->addr = (seL4_Word)vaddr;
    shmem_data->length_bytes = num_pages * PAGE_SIZE_4K;

    LINKED_LIST_PREPEND(shmem_data, handle->init_data.shmem_list_head);
    libprocess_return_success();
    libprocess_epilogue();
}


/**
 * Assumes you have error checked args
 */
static int copy_irq_to_proc(process_handle_t *handle,
                            seL4_CPtr ep_cap,
                            seL4_CPtr irq_cap,
                            seL4_Word irq_number,
                            const char *conn_name)
{
    libprocess_prologue();

    IrqData *irq_data = malloc(sizeof(IrqData));
    libprocess_guard(irq_data == NULL, -1, libprocess_epilogue,
                     "Failed to malloc Irq Data");

    irq_data__init(irq_data);
    irq_data->name = (char *)conn_name; /* protobuf uses non const strings */
    irq_data->irq_cap = libprocess_copy_cap_next_slot(handle, irq_cap, seL4_AllRights);
    libprocess_guard(irq_data->irq_cap != seL4_CapNull, -2, free_data, "Failed to copy IRQ cap");
    irq_data->ep_cap = libprocess_copy_cap_next_slot(handle, ep_cap, seL4_AllRights);
    libprocess_guard(irq_data->ep_cap != seL4_CapNull, -2, uncopy_irq_cap, "Failed to copy EP cap");
    irq_data->number = irq_number;

    LINKED_LIST_PREPEND(irq_data, handle->init_data.irq_list_head);
    libprocess_return_success();
    uncopy_irq_cap:
        libprocess_delete_cap_last_slot(handle);
    free_data:
        free(irq_data);
    libprocess_epilogue();
}

/**
 * Assumes you have error checked args
 */
static int copy_devmem_to_proc(process_handle_t *handle,
                               void *vaddr,
                               void *paddr,
                               seL4_Word num_pages,
                               seL4_Word page_bits,
                               seL4_CPtr *caps,
                               const char *device_name)
{
    libprocess_prologue();
    int current_cap = 0;

    DeviceMemoryData *devmem_data = malloc(sizeof(DeviceMemoryData));
    libprocess_guard(devmem_data == NULL, -1, libprocess_epilogue, 
                     "Failed to malloc device memory data");

    device_memory_data__init(devmem_data);
    devmem_data->name = (char *)device_name; /* protobuf uses non const strings */
    devmem_data->virt_addr = (seL4_Word)vaddr;
    devmem_data->phys_addr = (seL4_Word)paddr; 
    devmem_data->size_bits = page_bits;
    devmem_data->num_pages = num_pages;

    seL4_CPtr *new_caps = NULL;
    if(caps != NULL) {
        new_caps = malloc(sizeof(seL4_CPtr)*num_pages);
        libprocess_guard(new_caps == NULL, -9, free_data, 
                         "Failed to malloc new space for device caps");

        for(current_cap = 0; current_cap < num_pages; current_cap++) {
            new_caps[current_cap] = libprocess_copy_cap_next_slot(handle, caps[current_cap], seL4_AllRights); 
            libprocess_guard(new_caps[current_cap] != seL4_CapNull, -2, uncopy_caps, "Failed to copy cap");
        }

        /**
         * This check should get compiled out.
         * This deals with the fact that protobuf requires explicit sizes
         */
        if(sizeof(seL4_CPtr) == sizeof(uint32_t)) { 
            devmem_data->caps32 = (uint32_t*)new_caps;
            devmem_data->n_caps32 = num_pages;
        } else if(sizeof(seL4_CPtr) == sizeof(uint64_t)) {
            devmem_data->caps64 = (uint64_t*)new_caps;
            devmem_data->n_caps64 = num_pages;
        }
    }

    LINKED_LIST_PREPEND(devmem_data, handle->init_data.devmem_list_head);
    libprocess_return_success();

    uncopy_caps:
        --current_cap; // current_cap failed and does not need uncopying
        for(; current_cap >= 0; --current_cap) { libprocess_delete_cap_last_slot(handle); }
        free(new_caps);
    free_data:
        free(devmem_data);
    libprocess_epilogue();
}




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


static int connect_many_to_existing_generic(process_handle_t **handle_list,
                                            seL4_CapRights_t *perms_list,
                                            seL4_Word num_procs,
                                            copy_cap_to_proc_func_t copy_cap_to_proc,
                                            seL4_CPtr existing_cap,
                                            process_shared_objects_t *shobj,
                                            const char *conn_name)
{
    libprocess_prologue();
    libprocess_check_initialized();

    libprocess_check_arg(handle_list);
    libprocess_check_arg(perms_list);
    libprocess_check_arg(conn_name);

    int process_number = 0;
    process_shared_objects_ref_t *tmp = NULL;

    for(process_number = 0; process_number < num_procs; process_number++) {
        process_handle_t *handle = handle_list[process_number];

        if(handle == NULL) {
            ZF_LOGW("Null process handle in list, continuing");
            continue;
        }

        if(handle->state != PROCESS_INIT) {
            ZF_LOGW("Cannot modify/configure a running process, continuing");
            continue;
        }

        if(shobj != NULL) {
            tmp = malloc(sizeof(process_shared_objects_ref_t));
            libprocess_check_malloc(tmp, failed_malloc);
            tmp->ref = shobj;
            LINKED_LIST_PREPEND(tmp, handle->shared_objects);
        }

        libprocess_set_status(copy_cap_to_proc(handle, existing_cap, perms_list[process_number], conn_name));
        libprocess_error_guard(libprocess_get_status(), CAP_COPY_ERROR, failed_cap_copy);
    }

    libprocess_return_success();
failed_malloc:
failed_cap_copy:
    /* TODO: FAIL somewhat gracefully*/
    libprocess_epilogue();
}



static int process_map_device_pages_optional_caps(process_handle_t *handle, 
                                                  void *paddr,
                                                  seL4_Word num_pages,
                                                  seL4_Word page_bits,
                                                  const char* device_name,
                                                  bool add_caps)
{
    libprocess_prologue();

    libprocess_check_initialized();

    libprocess_check_arg(handle);
    libprocess_check_arg(device_name);

    libprocess_check_state(handle, PROCESS_INIT);

    ZF_LOGW_IF(!IS_ALIGNED((seL4_Word)paddr, page_bits),
               "Physical address of device not aligned to page boundaries.");

    seL4_CPtr *caps = malloc(sizeof(seL4_CPtr)*num_pages);
    libprocess_check_malloc(caps, libprocess_epilogue);

    mmap_entry_attr_t attrs = mmap_attr_4k_device;
    attrs.page_size_bits = page_bits;

    void * vaddr;
    reservation_t res; /* TODO: track this reservation to free later */
    libprocess_set_status(mmap_new_device_pages_custom(&handle->vspace,
                                                       handle->page_dir.cptr,
                                                       paddr,
                                                       num_pages,
                                                       &attrs,
                                                       caps,
                                                       &vaddr,
                                                       &res));
    libprocess_guard(libprocess_get_status(), -6, free_caps, "Failed to map device");


    libprocess_set_status(copy_devmem_to_proc(handle,
                                              vaddr,
                                              paddr,
                                              num_pages,
                                              page_bits,
                                              add_caps ? caps : NULL,
                                              device_name));
    libprocess_guard(libprocess_get_status(), -6, free_caps, 
                     "Failed to copy device memory to child");

    free(caps);
    libprocess_return_success();

    free_caps:
        free(caps);
    libprocess_epilogue();
}



static int process_map_my_device_pages_optional_caps(process_handle_t *handle, 
                                                     const char *device_name,
                                                     const char *new_device_name,
                                                     bool add_caps)
{
    libprocess_prologue();

    libprocess_check_initialized();

    libprocess_check_arg(device_name);
    libprocess_check_arg(new_device_name);
    libprocess_check_arg(handle);

    libprocess_check_state(handle, PROCESS_INIT);

    /**
     * Lookup device info in parent's init data
     */
    init_devmem_info_t info;
    libprocess_set_status(init_lookup_devmem_info(device_name, &info))
    libprocess_guard(libprocess_get_status(), -6, libprocess_epilogue,
                     "Failed to lookup device mem object");
    libprocess_guard(info.caps == NULL, -6, libprocess_epilogue,
                     "Failed to find caps for memory");

    mmap_entry_attr_t attrs = mmap_attr_4k_device;
    attrs.page_size_bits = info.size_bits;

    void * vaddr;
    reservation_t res;
    libprocess_set_status(mmap_existing_pages_custom(&handle->vspace,
                                                     handle->page_dir.cptr,
                                                     info.num_pages,
                                                     &attrs,
                                                     info.caps,
                                                     &vaddr,
                                                     &res));
    libprocess_guard(libprocess_get_status(), -6, libprocess_epilogue, "Failed to map device");

    
    libprocess_set_status(copy_devmem_to_proc(handle,
                                              info.vaddr,
                                              info.paddr,
                                              info.num_pages,
                                              info.size_bits,
                                              add_caps ? info.caps : NULL,
                                              new_device_name));
    libprocess_guard(libprocess_get_status(), -6, libprocess_epilogue, "Failed to copy device memory to child");

    libprocess_return_success();
    libprocess_epilogue();
}




/******************************************************************************
 *
 * DEVICE CONFIG
 *
 *****************************************************************************/

int process_map_device_pages(process_handle_t *handle,
                             void *paddr,
                             seL4_Word num_pages,
                             seL4_Word page_bits,
                             const char *device_name)
{
    return process_map_device_pages_optional_caps(handle,
                                                  paddr,
                                                  num_pages,
                                                  page_bits,
                                                  device_name,
                                                  false);
}



int process_map_device_pages_give_caps(process_handle_t *handle,
                                       void *paddr,
                                       seL4_Word num_pages,
                                       seL4_Word page_bits,
                                       const char *device_name)
{
    return process_map_device_pages_optional_caps(handle,
                                                  paddr,
                                                  num_pages,
                                                  page_bits,
                                                  device_name,
                                                  true);
}


int process_map_my_device(process_handle_t *handle,
                          const char *device_name,
                          const char *new_device_name)
{
    return process_map_my_device_pages_optional_caps(handle, 
                                                     device_name,
                                                     new_device_name,
                                                     false);

}

int process_map_my_device_give_caps(process_handle_t *handle,
                                    const char *device_name,
                                    const char *new_device_name)
{
    return process_map_my_device_pages_optional_caps(handle, 
                                                     device_name,
                                                     new_device_name,
                                                     true);

}

int process_add_device_irq(process_handle_t *handle,
                           int irq_number,
                           const char* device_name)
{
    libprocess_prologue();

    libprocess_check_initialized();

    libprocess_guard(init_objects.info == NULL, -6, libprocess_epilogue,
                     "This function can only be used by the root task");
    
    libprocess_check_arg(device_name);
    libprocess_check_arg(handle);

    libprocess_check_state(handle, PROCESS_INIT);

    seL4_CPtr irq_cap;
    cspacepath_t irq_path;
    
    /**
     * Since our cap slots are managed by vka, we need a spot
     * for simple to put our new irq cap.
     */
    libprocess_set_status(vka_cspace_alloc(&init_objects.vka, &irq_cap));
    libprocess_guard(libprocess_get_status(), -6, libprocess_epilogue,
                     "Failed to find a slot for the irq cap");
    vka_cspace_make_path(&init_objects.vka, irq_cap, &irq_path);

    /**
     * Get the irq cap using simple. This uses the bootinfo's seL4_CapIRQControl
     * cap to generate it.
     */
    libprocess_set_status(simple_get_IRQ_handler(&init_objects.simple, irq_number, irq_path));
    libprocess_guard(libprocess_get_status(), -6, free_cspace,
                     "Failed to get an IRQ handler cap from the IRQControl cap");

    /**
     * Allocate a notification object.
     */
    vka_object_t irq_notification;
    libprocess_set_status(vka_alloc_notification(&init_objects.vka, &irq_notification));
    libprocess_guard(libprocess_get_status(), -6, free_cspace,
                     "Failed to allocate a notification object");
    /**
     * bind the notification to our irq cap
     */
    libprocess_set_status(seL4_IRQHandler_SetNotification(irq_cap, irq_notification.cptr));
    libprocess_guard(libprocess_get_status(), -6, free_notification,
                     "Failed to bind our irq to the notification");

    /**
     * Enable IRQ and Ack any outstanding intterupts
     */
    seL4_IRQHandler_Ack(irq_cap);

    libprocess_set_status(copy_irq_to_proc(handle, irq_cap, irq_notification.cptr, irq_number, device_name));
    libprocess_guard(libprocess_get_status(), -6, free_notification,
                     "Failed to copy irq caps to proc");

    free_parent_cap(irq_cap);
    free_parent_cap(irq_notification.cptr);
    libprocess_return_success();

    free_notification:
        vka_free_object(&init_objects.vka, &irq_notification);
    free_cspace:
        vka_cspace_free(&init_objects.vka, irq_cap);
    libprocess_epilogue();

}


int process_add_my_device_irq(process_handle_t *handle,
                              const char *device_name,
                              const char *new_device_name)
{
    libprocess_prologue();

    libprocess_check_initialized();
    
    libprocess_check_arg(device_name);
    libprocess_check_arg(new_device_name);
    libprocess_check_arg(handle);

    libprocess_check_state(handle, PROCESS_INIT);

    init_irq_info_t info;
    libprocess_set_status(init_lookup_irq(device_name, &info));
    libprocess_guard(libprocess_get_status(), -6, libprocess_epilogue,
                     "Failed to lookup IRQ caps");
    
    libprocess_set_status(copy_irq_to_proc(handle, info.irq, info.ep, info.number, new_device_name));
    libprocess_guard(libprocess_get_status(), -6, libprocess_epilogue,
                     "Failed to copy irq caps to child");

    /**
     * TODO: do we free the parent's caps?
     */

    libprocess_return_success();
    libprocess_epilogue();
}
                              
/******************************************************************************
 *
 * UNTYPED CONFIG
 *
 *****************************************************************************/

int process_give_untyped_resources(process_handle_t *handle,
                                   seL4_Word size_bits,
                                   seL4_Word num_objects)
{
    libprocess_prologue();
    libprocess_check_initialized();

    libprocess_check_arg(handle);
    libprocess_check_state(handle, PROCESS_INIT);

    ZF_LOGV("Warning:\n"
            "\tAdding untyped memory to child process!"
            "\tThis may give it unexpected permissions.");
    
    int i = 0;
    process_object_t *ut = NULL;
    UntypedData *ut_data = NULL;

    for(; i < num_objects; i++) {
        
        ut = (process_object_t*)malloc(sizeof(process_object_t));
        libprocess_check_malloc(ut, failed_ut_malloc);

        libprocess_set_status(vka_alloc_untyped(&init_objects.vka, size_bits, &ut->obj));
        libprocess_guard(libprocess_get_status(), -6, failed_vka, "Failed to allocate ut object");
    
        ut_data = malloc(sizeof(UntypedData));
        libprocess_check_malloc(ut_data, failed_data_malloc);
    
        untyped_data__init(ut_data);
        ut_data->size = size_bits;
        ut_data->cap = libprocess_copy_cap_next_slot(handle, ut->obj.cptr, seL4_AllRights);
        libprocess_error_guard(ut_data->cap == seL4_CapNull, CAP_COPY_ERROR, failed_cap_copy);
        
        /* Push the ut data onto the list */
        LINKED_LIST_PREPEND(ut_data, handle->init_data.untyped_list_head);
        LINKED_LIST_PREPEND(ut, handle->untyped_allocation_list);
    }
    
    libprocess_return_success();
    failed_ut_malloc:
    --i; // We failed at the beginning of this iteration
    for(; i>=0; --i) {
        LINKED_LIST_POP(ut, handle->untyped_allocation_list);
        LINKED_LIST_POP(ut_data, handle->init_data.untyped_list_head);
        libprocess_delete_cap_last_slot(handle);
    failed_cap_copy:
        free(ut_data);
    failed_data_malloc:
        vka_free_object(&init_objects.vka, &ut->obj);
    failed_vka:
        free(ut);
    }
    libprocess_epilogue();
}



/******************************************************************************
 *
 * ENDPOINT CONNECTIONS
 *
 *****************************************************************************/

int process_connect_many_to_self_endpoint(process_handle_t **handle_list,
                                          seL4_CapRights_t *perms_list,
                                          seL4_Word num_procs,
                                          const char *conn_name,
                                          seL4_CPtr *new_self_cap)
{
    libprocess_prologue();

    libprocess_check_initialized();

    vka_object_t ep; /* TODO for now we leak the memory if parent wants to connect */
    libprocess_set_status(vka_alloc_endpoint(&init_objects.vka, &ep));
    libprocess_guard(libprocess_get_status(), -6, libprocess_epilogue,
                     "Failed to allocate endpoint object.");

    *new_self_cap = ep.cptr;
    libprocess_set_status(connect_many_to_existing_generic(handle_list,
                                                           perms_list,
                                                           num_procs,
                                                           copy_ep_to_proc,
                                                           ep.cptr,
                                                           NULL,
                                                           conn_name));
    libprocess_guard(libprocess_get_status(), -6, libprocess_epilogue,
                     "Failed to connect");

    libprocess_return_success();
    libprocess_epilogue();
}



int process_connect_many_to_endpoint(process_handle_t **handle_list,
                                     seL4_CapRights_t *perms_list,
                                     seL4_Word num_procs,
                                     const char *conn_name)
{
    libprocess_prologue();

    libprocess_check_initialized();

    vka_object_t *ep = malloc(sizeof(vka_object_t));
    libprocess_check_malloc(ep, libprocess_epilogue);

    process_shared_objects_t *new_obj = malloc(sizeof(process_shared_objects_t));
    libprocess_check_malloc(new_obj, free_endpoint);

    libprocess_set_status(vka_alloc_endpoint(&init_objects.vka, ep));
    libprocess_guard(libprocess_get_status(), -6, fail,
                     "Failed to allocate endpoint object.");

    /**
     * Even though we don't return it, we actually retain a cap to the endpoint for bookkeeping.
     */
    new_obj->ref_count = num_procs;
    new_obj->obj_list = ep;
    new_obj->num_objs = 1; 

    libprocess_set_status(connect_many_to_existing_generic(handle_list,
                                                           perms_list,
                                                           num_procs,
                                                           copy_ep_to_proc,
                                                           ep->cptr,
                                                           new_obj,
                                                           conn_name));
    libprocess_guard(libprocess_get_status(), -6, fail,
                     "Failed to connect");

    libprocess_return_success();
    fail:
        free(new_obj);
    free_endpoint:
        free(ep);
    libprocess_epilogue();
}



int process_connect_to_existing_endpoint(process_handle_t *handle,
                                         seL4_CPtr existing_cap,
                                         seL4_CapRights_t perms,
                                         const char *conn_name)
{
    return connect_many_to_existing_generic(&handle,
                                            &perms,
                                            1,
                                            copy_ep_to_proc,
                                            existing_cap,
                                            NULL,
                                            conn_name);
}



int process_connect_to_self_endpoint(process_handle_t *handle,
                                     seL4_CapRights_t perms,
                                     const char *conn_name,
                                     seL4_CPtr *new_cap)
{
    return process_connect_many_to_self_endpoint(&handle,
                                                 &perms,
                                                 1,
                                                 conn_name,
                                                 new_cap);
}



int process_connect_pair_to_endpoint(process_handle_t *handle1, seL4_CapRights_t perms1,
                                     process_handle_t *handle2, seL4_CapRights_t perms2,
                                     const char *conn_name)
{
    process_handle_t *handle_list[] = {handle1, handle2};
    seL4_CapRights_t perms_list[] =   {perms1,  perms2};

    return process_connect_many_to_endpoint(handle_list,
                                            perms_list,
                                            2,
                                            conn_name);
}



/******************************************************************************
 *
 * NOTIFICATION CONNECTIONS
 *
 *****************************************************************************/

int process_connect_many_to_self_notification(process_handle_t **handle_list,
                                              seL4_CapRights_t *perms_list,
                                              seL4_Word num_procs,
                                              const char *conn_name,
                                              seL4_CPtr *new_self_cap)
{
    libprocess_prologue();

    libprocess_check_initialized();

    vka_object_t ep;
    libprocess_set_status(vka_alloc_notification(&init_objects.vka, &ep));
    libprocess_guard(libprocess_get_status(), -6, libprocess_epilogue,
                     "Failed to allocate endpoint object");

    libprocess_set_status(connect_many_to_existing_generic(handle_list,
                                                           perms_list,
                                                           num_procs,
                                                           copy_notification_to_proc,
                                                           ep.cptr,
                                                           NULL,
                                                           conn_name));
    libprocess_guard(libprocess_get_status(), -6, fail,
                     "Failed to connect");

    *new_self_cap = ep.cptr;
    libprocess_return_success();
    fail:
        vka_free_object(&init_objects.vka, &ep);
    libprocess_epilogue();
}

int process_connect_many_to_notification(process_handle_t **handle_list,
                                         seL4_CapRights_t *perms_list,
                                         seL4_Word num_procs,
                                         const char *conn_name)
{
    libprocess_prologue();

    libprocess_check_initialized();

    vka_object_t *ep = malloc(sizeof(vka_object_t));
    libprocess_check_malloc(ep, libprocess_epilogue);

    process_shared_objects_t *new_obj = malloc(sizeof(process_shared_objects_t));
    libprocess_check_malloc(new_obj, failed_malloc);

    libprocess_set_status(vka_alloc_notification(&init_objects.vka, ep));
    libprocess_guard(libprocess_get_status(), -6, fail,
                     "Failed to allocate endpoint object.");

    /**
     * Even though we don't return it, we actually retain a cap to the endpoint for bookkeeping.
     */
    new_obj->ref_count = num_procs;
    new_obj->obj_list = ep;
    new_obj->num_objs = 1; 

    libprocess_set_status(connect_many_to_existing_generic(handle_list,
                                                           perms_list,
                                                           num_procs,
                                                           copy_notification_to_proc,
                                                           ep->cptr,
                                                           new_obj,
                                                           conn_name));
    libprocess_guard(libprocess_get_status(), -6, fail, "Failed to connect");

    libprocess_return_success();
    fail:
        free(ep);
    failed_malloc:
        free(new_obj);
    libprocess_epilogue();
}


int process_connect_to_existing_notification(process_handle_t *handle,
                                             seL4_CPtr existing_cap,
                                             seL4_CapRights_t perms,
                                             const char *conn_name)
{
    return connect_many_to_existing_generic(&handle,
                                            &perms,
                                            1,
                                            copy_notification_to_proc,
                                            existing_cap,
                                            NULL,
                                            conn_name);
}


int process_connect_to_self_notification(process_handle_t *handle,
                                     seL4_CapRights_t perms,
                                     const char *conn_name,
                                     seL4_CPtr *new_cap)
{
    return process_connect_many_to_self_notification(&handle,
                                                     &perms,
                                                     1,
                                                     conn_name,
                                                     new_cap);
}


int process_connect_pair_to_notification(process_handle_t *handle1, seL4_CapRights_t perms1,
                                         process_handle_t *handle2, seL4_CapRights_t perms2,
                                         const char *conn_name)
{
    process_handle_t *handle_list[] = {handle1, handle2};
    seL4_CapRights_t perms_list[] =   {perms1,  perms2};

    return process_connect_many_to_notification(handle_list,
                                                perms_list,
                                                2,
                                                conn_name);
}



/******************************************************************************
 *
 * SHMEM CONNECTIONS
 *
 *****************************************************************************/

int process_connect_many_to_self_shmem(process_handle_t **handle_list,
                                       seL4_CapRights_t *perms_list,
                                       seL4_Word num_procs,
                                       seL4_Word num_pages,
                                       const char *conn_name,
                                       void **new_ptr)
{
    libprocess_prologue();

    libprocess_check_initialized();

    libprocess_check_arg(handle_list);
    libprocess_check_arg(perms_list);
    libprocess_check_arg(conn_name);
    
    UNUSED int i, j;

    seL4_CPtr *caps = malloc(sizeof(seL4_CPtr)*num_pages);
    libprocess_check_malloc(caps, libprocess_epilogue);

    /**
     * We first map the pages into our own space.
     */
    mmap_entry_attr_t attrs = mmap_attr_4k_data;
    attrs.readable = 1;
    attrs.writable = 1;
    reservation_t res;
    libprocess_set_status(mmap_new_pages_custom(&init_objects.vspace,
                                                init_objects.page_dir_cap,
                                                num_pages,
                                                &attrs,
                                                caps,
                                                new_ptr,
                                                &res));
    libprocess_guard(libprocess_get_status(), -6, free_caps, 
                     "Failed to map new pages into parent");

    /**
     * Then map the pages into the list of children
     */
    for(i = 0; i < num_procs; i++) {
        process_handle_t *handle = handle_list[i];

        if(handle == NULL) {
            ZF_LOGW("Null process handle in list, continuing");
            continue;
        }
    
        if(handle->state != PROCESS_INIT) {
            ZF_LOGW("Process has already been started, continuing");
            continue;
        }

        /**
         * We need to copy the caps to map them again
         * This overwrites the caps array.
         */
        for(j = 0; j < num_pages; j++) {
            cspacepath_t path1, path2;
            vka_cspace_make_path(&init_objects.vka, caps[j], &path1);
            vka_cspace_alloc_path(&init_objects.vka, &path2);
            libprocess_set_status(vka_cnode_copy(&path2, &path1, seL4_AllRights));
            libprocess_guard(libprocess_get_status(), -6, cleanup,
                             "Failed to copy cap for shared page.");
            caps[j] = path2.capPtr;
        } 

        /* TODO allow executable mem sharing */
        attrs.readable = seL4_CapRights_get_capAllowRead(perms_list[i]);
        attrs.writable = seL4_CapRights_get_capAllowWrite(perms_list[i]);


        void *vaddr;
        reservation_t res; 
        libprocess_set_status(mmap_existing_pages_custom(&handle->vspace,
                                                         handle->page_dir.cptr,
                                                         num_pages,
                                                         &attrs,
                                                         caps,
                                                         &vaddr,
                                                         &res));
        libprocess_guard(libprocess_get_status(), -6, cleanup,
                         "Failed to share pages to child process");

        libprocess_set_status(copy_shmem_to_proc(handle, vaddr, num_pages, conn_name));
        libprocess_guard(libprocess_get_status(), -6, cleanup, "Failed to copy shemem data to child");
    }


    free(caps);
    libprocess_return_success();
    cleanup:
        /* TODO: If we fail, undo or set processes as invalid */
    free_caps:
        free(caps);
    libprocess_epilogue();
}


int process_connect_many_to_shmem(process_handle_t **handle_list,
                                  seL4_CapRights_t *perms_list,
                                  seL4_Word num_procs,
                                  seL4_Word num_pages,
                                  const char *conn_name)
{
    libprocess_prologue();
    UNUSED int i, j;

    libprocess_check_initialized();

    libprocess_check_arg(handle_list);
    libprocess_check_arg(perms_list);
    libprocess_check_arg(conn_name);

    seL4_CPtr *caps = malloc(sizeof(seL4_CPtr)*num_pages);
    libprocess_check_malloc(caps, libprocess_epilogue);

    process_shared_objects_t *new_objs = malloc(sizeof(process_shared_objects_t));
    libprocess_check_malloc(new_objs, free_caps);

    new_objs->obj_list = malloc(sizeof(vka_object_t)*num_pages);
    libprocess_check_malloc(new_objs->obj_list, free_new_objs);


    for(i = 0; i < num_pages; i++) {
        libprocess_set_status(vka_alloc_frame(&init_objects.vka, PAGE_BITS_4K, &new_objs->obj_list[i]));
        libprocess_guard(libprocess_get_status(), -6, failed, "Failed to allocate a page of memory from vka");
        caps[i] = new_objs->obj_list[i].cptr;
    }

    new_objs->ref_count = num_procs;
    new_objs->num_objs = num_pages;


    /**
     * Then map the pages into the list of children
     */
    for(i = 0; i < num_procs; i++) {
        process_handle_t *handle = handle_list[i];

        if(handle == NULL) {
            ZF_LOGW("Null process handle in list, continuing");
            new_objs->ref_count--;
            continue;
        }
    
        if(handle->state != PROCESS_INIT) {
            ZF_LOGW("Process has already been started, continuing");
            new_objs->ref_count--;
            continue;
        }

        /**
         * We need to copy the caps to map them again
         * (We also don't map the original caps).
         * This overwrites the caps array.
         */
        for(j = 0; j < num_pages; j++) {
            cspacepath_t path1, path2;
            vka_cspace_make_path(&init_objects.vka, caps[j], &path1);
            vka_cspace_alloc_path(&init_objects.vka, &path2);
            libprocess_set_status(vka_cnode_copy(&path2, &path1, seL4_AllRights));
            libprocess_guard(libprocess_get_status(), -6, failed, "Failed to copy cap for shared page.");
            caps[j] = path2.capPtr;
        }

        /**
         * Insert shared obj bookkeeping for this process handle
         */
        process_shared_objects_ref_t *tmp = malloc(sizeof(process_shared_objects_ref_t));
        libprocess_check_malloc(tmp, failed);
        tmp->ref = new_objs;
        LINKED_LIST_PREPEND(tmp, handle->shared_objects);


        /* TODO allow executable mem sharing */
        mmap_entry_attr_t attrs = mmap_attr_4k_data;
        attrs.readable = seL4_CapRights_get_capAllowRead(perms_list[i]);
        attrs.writable = seL4_CapRights_get_capAllowWrite(perms_list[i]);

        void *vaddr;

        reservation_t res; 
        libprocess_set_status(mmap_existing_pages_custom(&handle->vspace,
                                                         handle->page_dir.cptr,
                                                         num_pages,
                                                         &attrs,
                                                         caps,
                                                         &vaddr,
                                                         &res));
        libprocess_guard(libprocess_get_status(), -6, failed, "Failed to share pages to child process");
        
        libprocess_set_status(copy_shmem_to_proc(handle, vaddr, num_pages, conn_name));
        libprocess_guard(libprocess_get_status(), -6, failed, "Failed to copy shemem data to child");
    }

    /* TODO: Should we be freeing caps? */
    libprocess_return_success();

failed:
    /**
     * TODO: free vka objs, free each proc sh obj ref
     */
    free(new_objs->obj_list);
free_new_objs:
    free(new_objs);
free_caps:
    free(caps);
    libprocess_epilogue();
}


int process_connect_to_self_shmem(process_handle_t *handle,
                                  seL4_CapRights_t perms,
                                  seL4_Word num_pages,
                                  const char *conn_name,
                                  void** new_ptr)
{
    return process_connect_many_to_self_shmem(&handle,
                                              &perms,
                                              1,
                                              num_pages,
                                              conn_name,
                                              new_ptr);
}

int process_connect_pair_to_shmem(process_handle_t *handle1, seL4_CapRights_t perms1,
                                  process_handle_t *handle2, seL4_CapRights_t perms2,
                                  seL4_Word num_pages,
                                  const char *conn_name)
{
    process_handle_t *handle_list[] = {handle1, handle2};
    seL4_CapRights_t perms_list[] =   {perms1,  perms2};

    return process_connect_many_to_shmem(handle_list,
                                         perms_list,
                                         2,
                                         num_pages,
                                         conn_name);
}

