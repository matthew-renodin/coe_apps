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

/**
 * Definition for function pointer type.
 * Generic copy cap into process.
 */
typedef int (copy_cap_to_proc_func_t)(process_handle_t *handle,
                                      seL4_CPtr cap_to_copy,
                                      seL4_CapRights_t perms,
                                      const char* conn_name);


/**
 * Assumes you have error checked args
 */
static seL4_CPtr copy_cap_into_next_slot(process_handle_t *handle,
                                         seL4_CPtr new_cap,
                                         seL4_CapRights_t perms)
{
    int error;

    cspacepath_t dst, src;
    dst.root = handle->cnode.cptr;
    dst.capDepth = handle->attrs.cnode_size_bits;
    dst.capPtr = handle->cnode_next_free;

    vka_cspace_make_path(&init_objects.vka, new_cap, &src);
    error = vka_cnode_copy(&dst, &src, perms);
    if(error) {
        ZF_LOGE("Failed to copy cap into child cnode.");
        return seL4_CapNull;
    }

    return handle->cnode_next_free++;
    
}


/**
 * Assumes you have error checked args
 */
static int copy_ep_to_proc(process_handle_t *handle,
                           seL4_CPtr ep_cap,
                           seL4_CapRights_t perms,
                           const char *conn_name) 
{
    EndpointData *ep_data = malloc(sizeof(EndpointData));
    if(ep_data == NULL) {
        ZF_LOGE("Failed to malloc Endpoint Data");
        return -1;
    }

    endpoint_data__init(ep_data);
    ep_data->name = (char *)conn_name; /* protobuf uses non const strings */
    ep_data->cap = copy_cap_into_next_slot(handle, ep_cap, perms);
    if(ep_data->cap == seL4_CapNull) {
        ZF_LOGE("Failed to copy ep cap");
        return -2;
    }

    ep_data->next = handle->init_data.ep_list_head;
    handle->init_data.ep_list_head = ep_data;

    return 0;

}


/**
 * Assumes you have error checked args
 */
static int copy_notification_to_proc(process_handle_t *handle,
                                     seL4_CPtr ep_cap,
                                     seL4_CapRights_t perms,
                                     const char *conn_name) 
{
    EndpointData *ep_data = malloc(sizeof(EndpointData));
    if(ep_data == NULL) {
        ZF_LOGE("Failed to malloc Endpoint Data");
        return -1;
    }

    endpoint_data__init(ep_data);
    ep_data->name = (char *)conn_name; /* protobuf uses non const strings */
    ep_data->cap = copy_cap_into_next_slot(handle, ep_cap, perms);
    if(ep_data->cap == seL4_CapNull) {
        ZF_LOGE("Failed to copy ep cap");
        return -2;
    }

    /* Push the endpoint onto the list */
    ep_data->next = handle->init_data.notification_list_head;
    handle->init_data.notification_list_head = ep_data;

    return 0;

}


/**
 * Assumes you have error checked args
 */
static int copy_shmem_to_proc(process_handle_t *handle,
                              void *vaddr,
                              seL4_Word num_pages,
                              const char *conn_name) 
{
    SharedMemoryData *shmem_data = malloc(sizeof(SharedMemoryData));
    if(shmem_data == NULL) {
        ZF_LOGE("Failed to malloc Shmem Data");
        return -1;
    }

    shared_memory_data__init(shmem_data);
    shmem_data->name = (char *)conn_name; /* protobuf uses non const strings */
    shmem_data->addr = (seL4_Word)vaddr;
    shmem_data->length_bytes = num_pages * PAGE_SIZE_4K;

    /* Push the shmem data onto the list */
    shmem_data->next = handle->init_data.shmem_list_head;
    handle->init_data.shmem_list_head = shmem_data;

    return 0;

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
    IrqData *irq_data = malloc(sizeof(IrqData));
    if(irq_data == NULL) {
        ZF_LOGE("Failed to malloc Irq Data");
        return -1;
    }
    irq_data__init(irq_data);

    irq_data->name = (char *)conn_name; /* protobuf uses non const strings */
    irq_data->irq_cap = copy_cap_into_next_slot(handle, irq_cap, seL4_AllRights);
    irq_data->ep_cap = copy_cap_into_next_slot(handle, ep_cap, seL4_AllRights);
    irq_data->number = irq_number;

    /* Push onto irq list */
    irq_data->next = handle->init_data.irq_list_head;
    handle->init_data.irq_list_head = irq_data;

    return 0;
}

/**
 *
 */
static int copy_devmem_to_proc(process_handle_t *handle,
                               void *vaddr,
                               void *paddr,
                               seL4_Word num_pages,
                               seL4_Word page_bits,
                               seL4_CPtr *caps,
                               const char *device_name)
{

    DeviceMemoryData *devmem_data = malloc(sizeof(DeviceMemoryData));
    if(devmem_data == NULL) {
        ZF_LOGE("Failed to malloc device memory data");
        return -1;
    }
    device_memory_data__init(devmem_data);

    
    devmem_data->name = (char *)device_name; /* protobuf uses non const strings */
    devmem_data->virt_addr = (seL4_Word)vaddr;
    devmem_data->phys_addr = (seL4_Word)paddr; 
    devmem_data->size_bits = page_bits;
    devmem_data->num_pages = num_pages;

    seL4_CPtr *new_caps = NULL;
    if(caps != NULL) {
        new_caps = malloc(sizeof(seL4_CPtr)*num_pages);
        if(new_caps == NULL) {
            ZF_LOGE("Failed to malloc new space for device caps");
            return -9;
        }

        for(int i = 0; i < num_pages; i++) {
            new_caps[i] = copy_cap_into_next_slot(handle, caps[i], seL4_AllRights); 
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

    /* Push onto init_data list */
    devmem_data->next = handle->init_data.devmem_list_head;
    handle->init_data.devmem_list_head = devmem_data;

    return 0;
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
    int error;

    /* TODO: Implement sync for init objects */
    if(!init_objects.initialized) {
        ZF_LOGW("Init objects (vka, vspace) have not been setup.\n"
                "Run init_process or init_root_task to complete.");
        return -1;
    }

    if(handle_list == NULL || perms_list == NULL) {
        ZF_LOGE("Null process handle or perms list passed");
        return -2; /* TODO come up with error codes */
    }

    if(conn_name == NULL) {
        ZF_LOGE("Null pointer to name passed");
        return -3;
    }


    for(int i = 0; i < num_procs; i++) {
        process_handle_t *handle = handle_list[i];

        if(handle == NULL) {
            ZF_LOGW("Null process handle in list, continuing");
            continue;
        }

        if(handle->state != PROCESS_INIT) {
            ZF_LOGW("Cannot modify/configure a running process, continuing");
            continue;
        }

        if(shobj != NULL) {
            process_shared_objects_ref_t *tmp = handle->shared_objects;
            handle->shared_objects = malloc(sizeof(process_shared_objects_ref_t));
            if(handle->shared_objects == NULL) {
                ZF_LOGE("Failed to malloc shared object ref");
                handle->shared_objects = tmp;
                return -4;
            }
            handle->shared_objects->next = tmp;
            handle->shared_objects->ref = shobj;
        }

        error = copy_cap_to_proc(handle, existing_cap, perms_list[i], conn_name);
        if(error) {
            ZF_LOGE("Failed to copy cap to process");
            return -6;
        }
    }

    return 0;
}



static int process_map_device_pages_optional_caps(process_handle_t *handle, 
                                                  void *paddr,
                                                  seL4_Word num_pages,
                                                  seL4_Word page_bits,
                                                  const char* device_name,
                                                  bool add_caps)
{
    int error;

    /* TODO: Implement sync for init objects */
    if(!init_check_initialized()) {
        ZF_LOGW("Init objects (vka, vspace) have not been setup.\n"
                "Run init_process or init_root_task to complete.");
        return -1;
    }

    if(device_name == NULL) {
        ZF_LOGE("Null device name passed");
        return -3;
    }

    if(handle == NULL) {
        ZF_LOGE("Invalid process handle pointer passed.");
        return -2; /* TODO come up with error codes */
    }

    if(handle->state != PROCESS_INIT) {
        ZF_LOGW("Process is already running.");
        return -3; /* TODO come up with error codes */
    }


    ZF_LOGW_IF(!IS_ALIGNED((seL4_Word)paddr, page_bits),
               "Physical address of device not aligned to page boundaries.");



    seL4_CPtr *caps = malloc(sizeof(seL4_CPtr)*num_pages);
    if(caps == NULL) {
        ZF_LOGE("Failed to malloc an array to hold page caps");
        return -4;
    }

    mmap_entry_attr_t attrs = mmap_attr_4k_device;
    attrs.page_size_bits = page_bits;

    void * vaddr;
    reservation_t res; /* TODO: track this reservation to free later */
    error = mmap_new_device_pages_custom(&handle->vspace,
                                         handle->page_dir.cptr,
                                         paddr,
                                         num_pages,
                                         &attrs,
                                         caps,
                                         &vaddr,
                                         &res);
    if(error) {
        ZF_LOGE("Failed to map device");
        free(caps);
        return -5;
    }


    error = copy_devmem_to_proc(handle,
                                vaddr,
                                paddr,
                                num_pages,
                                page_bits,
                                add_caps ? caps : NULL,
                                device_name);
    if(error) {
        ZF_LOGE("Failed to copy device memory to child");
        free(caps);
        return -6;
    }

    free(caps);

    return 0;
}



static int process_map_my_device_pages_optional_caps(process_handle_t *handle, 
                                                     const char *device_name,
                                                     const char *new_device_name,
                                                     bool add_caps)
{
    int error;

    /* TODO: Implement sync for init objects */
    if(!init_objects.initialized) {
        ZF_LOGW("Init objects (vka, vspace) have not been setup.\n"
                "Run init_process or init_root_task to complete.");
        return -1;
    }

    if(device_name == NULL || new_device_name == NULL) {
        ZF_LOGE("Null device name passed");
        return -2;
    }

    if(handle == NULL) {
        ZF_LOGE("Invalid process handle pointer passed.");
        return -3; /* TODO come up with error codes */
    }

    if(handle->state != PROCESS_INIT) {
        ZF_LOGW("Process is already running.");
        return -4; /* TODO come up with error codes */
    }

    /**
     * Lookup device info in parent's init data
     */
    init_devmem_info_t info;
    error = init_lookup_devmem_info(device_name, &info);
    if(error) {
        ZF_LOGE("Failed to lookup device mem object");
        return -6;
    }

    if(info.caps == NULL) {
        ZF_LOGE("Failed to find caps for memory");
        return -7;
    }

    mmap_entry_attr_t attrs = mmap_attr_4k_device;
    attrs.page_size_bits = info.size_bits;

    void * vaddr;
    reservation_t res; /* TODO: track this reservation to free later */
    error = mmap_existing_pages_custom(&handle->vspace,
                                       handle->page_dir.cptr,
                                       info.num_pages,
                                       &attrs,
                                       info.caps,
                                       &vaddr,
                                       &res);
    if(error) {
        ZF_LOGE("Failed to map device");
        return -8;
    }

    
    error = copy_devmem_to_proc(handle,
                                info.vaddr,
                                info.paddr,
                                info.num_pages,
                                info.size_bits,
                                add_caps ? info.caps : NULL,
                                new_device_name);
    if(error) {
        ZF_LOGE("Failed to copy device memory to child");
        return -9;
    }

    return 0;
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
    int error;

    /* TODO: Implement sync for init objects */
    if(!init_check_initialized()) {
        ZF_LOGW("Init objects (vka, vspace) have not been setup.\n"
                "Run init_process or init_root_task to complete.");
        return -1;
    }

    if(init_objects.info == NULL) {
        ZF_LOGE("This function can only be used by the root task");
        return -2;
    }

    if(device_name == NULL) {
        ZF_LOGE("Null device name passed");
        return -3;
    }

    if(handle == NULL) {
        ZF_LOGE("Invalid process handle pointer passed.");
        return -4; /* TODO come up with error codes */
    }
    
    if(handle->state != PROCESS_INIT) {
        ZF_LOGW("Process is already running.");
        return -5; /* TODO come up with error codes */
    }

    seL4_CPtr irq_cap;
    cspacepath_t irq_path;
    
    /**
     * Since our cap slots are managed by vka, we need a spot
     * for simple to put our new irq cap.
     */
    error = vka_cspace_alloc(&init_objects.vka, &irq_cap);
    if(error) {
        ZF_LOGE("Failed to find a slot for the irq cap");
        return -5;
    }
    vka_cspace_make_path(&init_objects.vka, irq_cap, &irq_path);

    /**
     * Get the irq cap using simple. This uses the bootinfo's seL4_CapIRQControl
     * cap to generate it.
     */
    error = simple_get_IRQ_handler(&init_objects.simple, irq_number, irq_path);
    if(error) {
        ZF_LOGE("Failed to get an IRQ handler cap from the IRQControl cap");
        return -6;
    }

    /**
     * Allocate a notification object.
     */
    vka_object_t irq_notification;
    error = vka_alloc_notification(&init_objects.vka, &irq_notification);
    if(error) {
        ZF_LOGE("Failed to allocate a notification object");
        return -7;
    }
    /**
     * bind the notification to our irq cap
     */
    error = seL4_IRQHandler_SetNotification(irq_cap, irq_notification.cptr);
    if(error) {
        ZF_LOGE("Failed to bind our irq to the notification");
        return -8;
    }

    /**
     * Enable IRQ and Ack any outstanding intterupts
     */
    seL4_IRQHandler_Ack(irq_cap);

    error = copy_irq_to_proc(handle, irq_cap, irq_notification.cptr, irq_number, device_name);
    if(error) {
        ZF_LOGE("Failed to copy irq caps to proc");
        return -9;
    }

    free_parent_cap(irq_cap);
    free_parent_cap(irq_notification.cptr);

    return 0;
}


int process_add_my_device_irq(process_handle_t *handle,
                              const char *device_name,
                              const char *new_device_name)
{
    int error;

    /* TODO: Implement sync for init objects */
    if(!init_objects.initialized) {
        ZF_LOGW("Init objects (vka, vspace) have not been setup.\n"
                "Run init_process or init_root_task to complete.");
        return -1;
    }

    if(device_name == NULL || new_device_name == NULL) {
        ZF_LOGE("Null device name passed");
        return -3;
    }

    if(handle == NULL) {
        ZF_LOGE("Invalid process handle pointer passed.");
        return -4; /* TODO come up with error codes */
    }
    
    if(handle->state != PROCESS_INIT) {
        ZF_LOGW("Process is already running.");
        return -5; /* TODO come up with error codes */
    }

    init_irq_info_t info;
    error = init_lookup_irq(device_name, &info);
    
    error = copy_irq_to_proc(handle, info.irq, info.ep, info.number, new_device_name);
    if(error) {
        ZF_LOGE("Failed to copy irq caps to child");
        return -6;
    }

    /**
     * TODO: do we free the parent's caps?
     */

    return 0;
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
    int error;

    /* TODO: Implement sync for init objects */
    if(!init_check_initialized()) {
        ZF_LOGW("Init objects (vka, vspace) have not been setup.\n"
                "Run init_process or init_root_task to complete.");
        return -1;
    }

    if(handle == NULL) {
        ZF_LOGE("Invalid process handle pointer passed to process_give_untyped_resources.");
        return -2; /* TODO come up with error codes */
    }

    if(handle->state != PROCESS_INIT) {
        ZF_LOGW("Process has already been started.");
        return -3;
    }

    ZF_LOGV("Warning:\n"
            "\tAdding untyped memory to child process!"
            "\tThis may give it unexpected permissions.");


    for(seL4_Word i = 0; i < num_objects; i++) {
        
        process_object_t *ut = (process_object_t*)malloc(sizeof(process_object_t));
        if(ut == NULL) {
            ZF_LOGE("Failed to malloc an untyped process object");
            return -4;
        }

        error = vka_alloc_untyped(&init_objects.vka, size_bits, &ut->obj);
        if(error) {
            ZF_LOGE("Failed to allocate ut object.");
            return -5;
        }
    
        UntypedData *ut_data = malloc(sizeof(UntypedData));
        if(ut_data == NULL) {
            ZF_LOGE("Failed to allocate Untyped Data");
            return -6;
        }
    
        untyped_data__init(ut_data);
        ut_data->size = size_bits;
        ut_data->cap = copy_cap_into_next_slot(handle, ut->obj.cptr, seL4_AllRights); 
        if(ut_data->cap == seL4_CapNull) {
            ZF_LOGE("Failed to copy ut cap");
            return -7;
        }
        
        /* Push the ut data onto the list */
        ut_data->next = handle->init_data.untyped_list_head;
        handle->init_data.untyped_list_head = ut_data;

        ut->next = handle->untyped_allocation_list;
        handle->untyped_allocation_list = ut;
    }
    
    return 0;
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
    int error;

    /* TODO: Implement sync for init objects */
    if(!init_check_initialized()) {
        ZF_LOGW("Init objects (vka, vspace) have not been setup.\n"
                "Run init_process or init_root_task to complete.");
        return -1;
    }

    vka_object_t ep; /* TODO for now we leak the memory if parent wants to connect */
    error = vka_alloc_endpoint(&init_objects.vka, &ep);
    if(error) {
        ZF_LOGE("Failed to allocate endpoint object.");
        return -2;
    }

    *new_self_cap = ep.cptr;

    error = connect_many_to_existing_generic(handle_list,
                                             perms_list,
                                             num_procs,
                                             copy_ep_to_proc,
                                             ep.cptr,
                                             NULL,
                                             conn_name);
    if(error) {
        ZF_LOGE("Failed to connect");
        return -5;
    }

    return 0;
}



int process_connect_many_to_endpoint(process_handle_t **handle_list,
                                     seL4_CapRights_t *perms_list,
                                     seL4_Word num_procs,
                                     const char *conn_name)
{
    int error;

    /* TODO: Implement sync for init objects */
    if(!init_check_initialized()) {
        ZF_LOGW("Init objects (vka, vspace) have not been setup.\n"
                "Run init_process or init_root_task to complete.");
        return -1;
    }

    vka_object_t *ep = malloc(sizeof(vka_object_t));
    if(ep == NULL) {
        ZF_LOGE("Failed to malloc ep data");
        return -2;
    }

    process_shared_objects_t *new_obj = malloc(sizeof(process_shared_objects_t));
    if(new_obj == NULL) {
        ZF_LOGE("Failed to malloc shared object data");
        free(ep);
        return -3;
    }

    error = vka_alloc_endpoint(&init_objects.vka, ep);
    if(error) {
        ZF_LOGE("Failed to allocate endpoint object.");
        free(ep);
        free(new_obj);
        return -4;
    }

    /**
     * Even though we don't return it, we actually retain a cap to the endpoint for bookkeeping.
     */
    new_obj->ref_count = num_procs;
    new_obj->obj_list = ep;
    new_obj->num_objs = 1; 

    error = connect_many_to_existing_generic(handle_list,
                                             perms_list,
                                             num_procs,
                                             copy_ep_to_proc,
                                             ep->cptr,
                                             new_obj,
                                             conn_name);
    if(error) {
        ZF_LOGE("Failed to connect");
        free(ep);
        free(new_obj);
        return -5;
    }

    return 0;
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
    int error;

    /* TODO: Implement sync for init objects */
    if(!init_check_initialized()) {
        ZF_LOGW("Init objects (vka, vspace) have not been setup.\n"
                "Run init_process or init_root_task to complete.");
        return -1;
    }

    vka_object_t ep;
    error = vka_alloc_notification(&init_objects.vka, &ep);
    if(error) {
        ZF_LOGE("Failed to allocate endpoint object.");
        return -2;
    }

    error = connect_many_to_existing_generic(handle_list,
                                             perms_list,
                                             num_procs,
                                             copy_notification_to_proc,
                                             ep.cptr,
                                             NULL,
                                             conn_name);

    *new_self_cap = ep.cptr;
    return 0;
}

int process_connect_many_to_notification(process_handle_t **handle_list,
                                         seL4_CapRights_t *perms_list,
                                         seL4_Word num_procs,
                                         const char *conn_name)
{
    int error;

    /* TODO: Implement sync for init objects */
    if(!init_check_initialized()) {
        ZF_LOGW("Init objects (vka, vspace) have not been setup.\n"
                "Run init_process or init_root_task to complete.");
        return -1;
    }

    vka_object_t *ep = malloc(sizeof(vka_object_t));
    if(ep == NULL) {
        ZF_LOGE("Failed to malloc ep data");
        return -2;
    }

    process_shared_objects_t *new_obj = malloc(sizeof(process_shared_objects_t));
    if(new_obj == NULL) {
        ZF_LOGE("Failed to malloc shared object data");
        free(ep);
        return -3;
    }

    error = vka_alloc_notification(&init_objects.vka, ep);
    if(error) {
        ZF_LOGE("Failed to allocate endpoint object.");
        free(ep);
        free(new_obj);
        return -4;
    }

    /**
     * Even though we don't return it, we actually retain a cap to the endpoint for bookkeeping.
     */
    new_obj->ref_count = num_procs;
    new_obj->obj_list = ep;
    new_obj->num_objs = 1; 

    error = connect_many_to_existing_generic(handle_list,
                                             perms_list,
                                             num_procs,
                                             copy_notification_to_proc,
                                             ep->cptr,
                                             new_obj,
                                             conn_name);
    if(error) {
        ZF_LOGE("Failed to connect");
        free(ep);
        free(new_obj);
        return -5;
    }

    return 0;
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
    UNUSED int error, i, j;

    /* TODO: Implement sync for init objects */
    if(!init_check_initialized()) {
        ZF_LOGW("Init objects (vka, vspace) have not been setup.\n"
                "Run init_process or init_root_task to complete.");
        return -1;
    }

    if(handle_list == NULL || perms_list == NULL) {
        ZF_LOGE("Null process handle or perms list passed");
        return -2; /* TODO come up with error codes */
    }

    if(conn_name == NULL) {
        ZF_LOGE("Null pointer to name passed");
        return -3;
    }

    seL4_CPtr *caps = malloc(sizeof(seL4_CPtr)*num_pages);
    if(caps == NULL) {
        ZF_LOGE("Failed to malloc temporary space for page caps");
        return -4;
    }

    /**
     * We first map the pages into our own space.
     */
    mmap_entry_attr_t attrs = mmap_attr_4k_data;
    attrs.readable = 1;
    attrs.writable = 1;
    reservation_t res;
    error = mmap_new_pages_custom(&init_objects.vspace,
                                  init_objects.page_dir_cap,
                                  num_pages,
                                  &attrs,
                                  caps,
                                  new_ptr,
                                  &res);
    if(error) {
        ZF_LOGE("Failed to map new pages into parent");
        free(caps);
        return -5;
    }

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
            error = vka_cnode_copy(&path2, &path1, seL4_AllRights);
            if(error) {
                ZF_LOGE("Failed to copy cap for shared page.");
                free(caps);
                return -6;
            }
            caps[j] = path2.capPtr;
        } 

        /* TODO allow executable mem sharing */
        attrs.readable = seL4_CapRights_get_capAllowRead(perms_list[i]);
        attrs.writable = seL4_CapRights_get_capAllowWrite(perms_list[i]);


        void *vaddr;
        reservation_t res; 
        error = mmap_existing_pages_custom(&handle->vspace,
                                           handle->page_dir.cptr,
                                           num_pages,
                                           &attrs,
                                           caps,
                                           &vaddr,
                                           &res);
        if(error) {
            ZF_LOGE("Failed to share pages to child process");
            free(caps);
            return -7;
        }

        error = copy_shmem_to_proc(handle, vaddr, num_pages, conn_name);
        if(error) {
            ZF_LOGE("Failed to copy shemem data to child");
            free(caps);
            return -8;
        }

    }


    free(caps);
    return 0;
}


int process_connect_many_to_shmem(process_handle_t **handle_list,
                                  seL4_CapRights_t *perms_list,
                                  seL4_Word num_procs,
                                  seL4_Word num_pages,
                                  const char *conn_name)
{
    UNUSED int error, i, j;

    /* TODO: Implement sync for init objects */
    if(!init_objects.initialized) {
        ZF_LOGW("Init objects (vka, vspace) have not been setup.\n"
                "Run init_process or init_root_task to complete.");
        return -1;
    }

    if(handle_list == NULL || perms_list == NULL) {
        ZF_LOGE("Null process handle or perms list passed");
        return -2; /* TODO come up with error codes */
    }

    if(conn_name == NULL) {
        ZF_LOGE("Null pointer to name passed");
        return -3;
    }

    seL4_CPtr *caps = malloc(sizeof(seL4_CPtr)*num_pages);
    if(caps == NULL) {
        ZF_LOGE("Failed to malloc temporary space for page caps");
        goto failed_caps;
    }

    process_shared_objects_t *new_objs = malloc(sizeof(process_shared_objects_t));
    if(new_objs == NULL) {
        ZF_LOGE("Failed to malloc shared objects struct");
        goto failed_new_objs;
    }

    new_objs->obj_list = malloc(sizeof(vka_object_t)*num_pages);
    if(new_objs->obj_list == NULL) {
        ZF_LOGE("Failed to malloc bookkeeping space for pages");
        goto failed_obj_list;
    }


    for(i = 0; i < num_pages; i++) {
        error = vka_alloc_frame(&init_objects.vka, PAGE_BITS_4K, &new_objs->obj_list[i]);
        if(error) {
            ZF_LOGE("Failed to allocate a page of memory from vka");
            goto failed;
        }
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
            error = vka_cnode_copy(&path2, &path1, seL4_AllRights);
            if(error) {
                ZF_LOGE("Failed to copy cap for shared page.");
                goto failed; 
            }
            caps[j] = path2.capPtr;
        }

        /**
         * Insert shared obj bookkeeping for this process handle
         */
        process_shared_objects_ref_t *sh_obj_list_old = handle->shared_objects;
        handle->shared_objects = malloc(sizeof(process_shared_objects_ref_t));
        if(handle->shared_objects == NULL) {
            ZF_LOGE("Failed to malloc memory for a shared object reference");
            goto failed;
        }
        handle->shared_objects->next = sh_obj_list_old;
        handle->shared_objects->ref = new_objs;


        /* TODO allow executable mem sharing */
        mmap_entry_attr_t attrs = mmap_attr_4k_data;
        attrs.readable = seL4_CapRights_get_capAllowRead(perms_list[i]);
        attrs.writable = seL4_CapRights_get_capAllowWrite(perms_list[i]);

        void *vaddr;

        reservation_t res; 
        error = mmap_existing_pages_custom(&handle->vspace,
                                           handle->page_dir.cptr,
                                           num_pages,
                                           &attrs,
                                           caps,
                                           &vaddr,
                                           &res);
        if(error) {
            ZF_LOGE("Failed to share pages to child process");
            goto failed;
        }
        
        error = copy_shmem_to_proc(handle, vaddr, num_pages, conn_name);
        if(error) {
            ZF_LOGE("Failed to copy shemem data to child");
            goto failed;
        }

    }

    return 0;

failed:
    /**
     * TODO: free vka objs, free each proc sh obj ref
     */
failed_obj_list:
    free(new_objs->obj_list);
failed_new_objs:
    free(new_objs);
failed_caps:
    free(caps);
    return -1;
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
