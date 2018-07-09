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
 * Assumes that handle is valid and that you have all the locks necessarry
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
 * This helper assumes you have grabbed all the locks
 */
static int copy_ep_to_proc(process_handle_t *handle,
                           seL4_CPtr ep_cap,
                           seL4_CapRights_t perms,
                           const char *conn_name) 
{
    EndpointData *ep_data = malloc(sizeof(EndpointData));
    if(ep_data == NULL) {
        ZF_LOGE("Failed to allocate Endpoint Data");
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
 * This helper assumes you have grabbed all the locks
 */
static int copy_notification_to_proc(process_handle_t *handle,
                                     seL4_CPtr ep_cap,
                                     seL4_CapRights_t perms,
                                     const char *conn_name) 
{
    EndpointData *ep_data = malloc(sizeof(EndpointData));
    if(ep_data == NULL) {
        ZF_LOGE("Failed to allocate Endpoint Data");
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
 * This helper assumes you have grabbed all the locks
 */
static int copy_shmem_to_proc(process_handle_t *handle,
                              void *vaddr,
                              seL4_Word num_pages,
                              const char *conn_name) 
{
    SharedMemoryData *shmem_data = malloc(sizeof(SharedMemoryData));
    if(shmem_data == NULL) {
        ZF_LOGE("Failed to allocate Endpoint Data");
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



static int process_map_device_pages_optional_caps(process_handle_t *handle, 
                                                  void *paddr,
                                                  seL4_Word num_pages,
                                                  seL4_Word page_bits,
                                                  const char* device_name,
                                                  bool add_caps)
{
    int error, i;

    /* TODO: Implement sync for init objects */
    if(!init_objects.initialized) {
        ZF_LOGW("Init objects (vka, vspace) have not been setup.\n"
                "Run init_process or init_root_task to complete.");
        return -1;
    }

    if(handle == NULL) {
        ZF_LOGE("Invalid process handle pointer passed to process_give_untyped_resources.");
        return -2; /* TODO come up with error codes */
    }

    if(handle->running) {
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

    error = mmap_new_device_pages_custom(&handle->vspace,
                                         handle->page_dir.cptr,
                                         paddr,
                                         num_pages,
                                         &attrs,
                                         caps,
                                         &vaddr);
    if(error) {
        ZF_LOGE("Failed to map device");
        return -5;
    }

    /**
     * We need to copy each parent cap into the child's cnode (overwriting caps[])
     * After this caps is filled with CPtrs relative to the childs cnode.
     */
    for(i = 0; i < num_pages; i++) {
        seL4_CPtr tmp = caps[i];
        if(add_caps) {
            caps[i] = copy_cap_into_next_slot(handle, caps[i], seL4_AllRights); 
        }
        free_parent_cap(tmp);
    }
    

    /**
     * Setup the init data
     */
    DeviceMemoryData *devmem_data = malloc(sizeof(DeviceMemoryData));
    device_memory_data__init(devmem_data);

    devmem_data->name = (char *)device_name; /* protobuf uses non const strings */
    devmem_data->virt_addr = (seL4_Word)vaddr;
    devmem_data->phys_addr = (seL4_Word)paddr; 
    devmem_data->size_bits = page_bits;
    devmem_data->num_pages = num_pages;
    if(add_caps) {
        devmem_data->caps = caps;
        devmem_data->n_caps = num_pages;
    }
    /* Push onto init_data list */
    devmem_data->next = handle->init_data.devmem_list_head;
    handle->init_data.devmem_list_head = devmem_data;

    return 0;
}



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



int process_add_device_irq(process_handle_t *handle,
                           int irq_number,
                           const char* device_name)
{
    int error;

    /* TODO: Implement sync for init objects */
    if(!init_objects.initialized) {
        ZF_LOGW("Init objects (vka, vspace) have not been setup.\n"
                "Run init_process or init_root_task to complete.");
        return -1;
    }

    if(handle == NULL) {
        ZF_LOGE("Invalid process handle pointer passed to process_give_untyped_resources.");
        return -2; /* TODO come up with error codes */
    }

    if(init_objects.info == NULL) {
        ZF_LOGE("This function can only be used by the root task");
        return -3;
    }
    
    if(handle->running) {
        ZF_LOGW("Process is already running.");
        return -4; /* TODO come up with error codes */
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

    /**
     * Setup init data. Copy caps to child process
     */
    IrqData *irq_data = malloc(sizeof(IrqData));
    irq_data__init(irq_data);

    irq_data->name = (char *)device_name; /* protobuf uses non const strings */
    irq_data->irq_cap = copy_cap_into_next_slot(handle, irq_cap, seL4_AllRights);
    irq_data->ep_cap = copy_cap_into_next_slot(handle, irq_notification.cptr, seL4_AllRights);
    irq_data->number = irq_number;
    /* Push onto irq list */
    irq_data->next = handle->init_data.irq_list_head;
    handle->init_data.irq_list_head = irq_data;

    free_parent_cap(irq_cap);
    free_parent_cap(irq_notification.cptr);

    return 0;
}



int process_connect_ep(process_handle_t *handle1, seL4_CapRights_t perms1,
                       process_handle_t *handle2, seL4_CapRights_t perms2,
                       const char *conn_name)
{
    int error;

    /* TODO: Implement sync for init objects */
    if(!init_objects.initialized) {
        ZF_LOGW("Init objects (vka, vspace) have not been setup.\n"
                "Run init_process or init_root_task to complete.");
        return -1;
    }

    if(handle1 == NULL || handle2 == NULL) {
        ZF_LOGE("Invalid process handle pointer passed to process_connect_ep.");
        return -2; /* TODO come up with error codes */
    }
    
    if(handle1->running || handle2->running) {
        ZF_LOGW("Process is already running.");
        return -3; /* TODO come up with error codes */
    }


    vka_object_t ep;
    error = vka_alloc_endpoint(&init_objects.vka, &ep);
    if(error) {
        ZF_LOGE("Failed to allocate ep object.");
        return -4;
    }

    error = copy_ep_to_proc(handle1, ep.cptr, perms1, conn_name);
    if(error) {
        ZF_LOGE("Failed to copy ep to process 1");
        vka_free_object(&init_objects.vka, &ep);
        return -5;
    }
    
    error = copy_ep_to_proc(handle2, ep.cptr, perms2, conn_name);
    if(error) {
        ZF_LOGE("Failed to copy ep to process 2");
        vka_free_object(&init_objects.vka, &ep);
        return -6;
    }

    free_parent_cap(ep.cptr);

    return 0;
}



int process_connect_notification(process_handle_t *handle1, seL4_CapRights_t perms1,
                                 process_handle_t *handle2, seL4_CapRights_t perms2,
                                 const char *conn_name)
{
    int error;

    /* TODO: Implement sync for init objects */
    if(!init_objects.initialized) {
        ZF_LOGW("Init objects (vka, vspace) have not been setup.\n"
                "Run init_process or init_root_task to complete.");
        return -1;
    }

    if(handle1 == NULL || handle2 == NULL) {
        ZF_LOGE("Invalid process handle pointer passed to process_connect_notification.");
        return -2; /* TODO come up with error codes */
    }

    if(handle1->running || handle2->running) {
        ZF_LOGW("Process is already running.");
        return -3; /* TODO come up with error codes */
    }

    vka_object_t ep;
    error = vka_alloc_notification(&init_objects.vka, &ep);
    if(error) {
        ZF_LOGE("Failed to allocate notification object.");
        return -4;
    }

    error = copy_notification_to_proc(handle1, ep.cptr, perms1, conn_name);
    if(error) {
        ZF_LOGE("Failed to copy notification to process 1");
        vka_free_object(&init_objects.vka, &ep);
        return -5;
    }
    
    error = copy_notification_to_proc(handle2, ep.cptr, perms2, conn_name);
    if(error) {
        ZF_LOGE("Failed to copy notification to process 2");
        vka_free_object(&init_objects.vka, &ep);
        return -6;
    }

    free_parent_cap(ep.cptr);
    return 0;
}



int process_connect_shmem(process_handle_t *handle1, seL4_CapRights_t perms1, 
                          process_handle_t *handle2, seL4_CapRights_t perms2,
                          seL4_Word num_pages,
                          const char *conn_name)
{
    int error, i;

    /* TODO: Implement sync for init objects */
    if(!init_objects.initialized) {
        ZF_LOGW("Init objects (vka, vspace) have not been setup.\n"
                "Run init_process or init_root_task to complete.");
        return -1;
    }

    if(handle1 == NULL || handle2 == NULL) {
        ZF_LOGE("Invalid process handle pointer passed to process_connect_shmem.");
        return -2; /* TODO come up with error codes */
    }

    if(handle1->running || handle2->running) {
        ZF_LOGW("Process is already running.");
        return -3; /* TODO come up with error codes */
    }

    mmap_entry_attr_t attrs = mmap_attr_4k_data;
    attrs.readable = seL4_CapRights_get_capAllowRead(perms1);
    attrs.writable = seL4_CapRights_get_capAllowWrite(perms1);

    void *vaddr1, *vaddr2;

    seL4_CPtr *caps = malloc(sizeof(seL4_CPtr)*num_pages);
    if(caps == NULL) {
        ZF_LOGE("Failed to malloc temporary space for page caps");
        return -4;
    }

    /**
     * First map the pages into handle1
     */
    error = mmap_new_pages_custom(&handle1->vspace,
                                  handle1->page_dir.cptr,
                                  num_pages,
                                  &attrs,
                                  caps,
                                  &vaddr1);
    if(error) {
        ZF_LOGE("Failed to map new pages into child");
        free(caps);
        return -5;
    }

    /**
     * We need to copy the caps to double map them.
     * This overwrites the caps array.
     */
    for(i = 0; i < num_pages; i++) {
        cspacepath_t path1, path2;
        vka_cspace_make_path(&init_objects.vka, caps[i], &path1);
        vka_cspace_alloc_path(&init_objects.vka, &path2);
        error = vka_cnode_copy(&path2, &path1, seL4_AllRights);
        if(error) {
            ZF_LOGE("Failed to copy cap for shared page.");
            free(caps);
            return -6;
        }
        free_parent_cap(caps[i]);
        caps[i] = path2.capPtr;
    }

    /**
     * Now we share map the pages into handle2
     */
    attrs.readable = seL4_CapRights_get_capAllowRead(perms2);
    attrs.writable = seL4_CapRights_get_capAllowWrite(perms2);

    error = mmap_existing_pages_custom(&handle2->vspace,
                                       handle2->page_dir.cptr,
                                       num_pages,
                                       &attrs,
                                       caps,
                                       &vaddr2);
    if(error) {
        ZF_LOGE("Failed to share pages to second process");
        free(caps);
        return -7;
    }

    for(i = 0; i < num_pages; i++) {
        free_parent_cap(caps[i]);
    }
    free(caps);


    error = copy_shmem_to_proc(handle1, vaddr1, num_pages, conn_name);
    if(error) {
        ZF_LOGE("Failed to copy shemem data to proc");
        return -8;
    }

    error = copy_shmem_to_proc(handle2, vaddr2, num_pages, conn_name);
    if(error) {
        ZF_LOGE("Failed to copy shemem data to proc");
        return -9;
    }

    return 0;
}


int process_give_untyped_resources(process_handle_t *handle,
                                   seL4_Word size_bits,
                                   seL4_Word num_objects)
{
    int error;

    /* TODO: Implement sync for init objects */
    if(!init_objects.initialized) {
        ZF_LOGW("Init objects (vka, vspace) have not been setup.\n"
                "Run init_process or init_root_task to complete.");
        return -1;
    }

    if(handle == NULL) {
        ZF_LOGE("Invalid process handle pointer passed to process_give_untyped_resources.");
        return -2; /* TODO come up with error codes */
    }

    if(handle->running) {
        ZF_LOGW("Process is already running.");
        return -3; /* TODO come up with error codes */
    }

    ZF_LOGV("Warning:\n"
            "\tAdding untyped memory to child process!"
            "\tThis may give it unexpected permissions.");


    for(seL4_Word i = 0; i < num_objects; i++) {
    
        vka_object_t ut;
        error = vka_alloc_untyped(&init_objects.vka, size_bits, &ut);
        if(error) {
            ZF_LOGE("Failed to allocate ut object.");
            return -4;
        }
    
        UntypedData *ut_data = malloc(sizeof(UntypedData));
        if(ut_data == NULL) {
            ZF_LOGE("Failed to allocate Untyped Data");
            return -5;
        }
    
        untyped_data__init(ut_data);
        ut_data->size = size_bits;
        ut_data->cap = copy_cap_into_next_slot(handle, ut.cptr, seL4_AllRights); /* TODO:perms?*/
        if(ut_data->cap == seL4_CapNull) {
            ZF_LOGE("Failed to copy ut cap");
            return -6;
        }
        
        /* Push the ut data onto the list */
        ut_data->next = handle->init_data.untyped_list_head;
        handle->init_data.untyped_list_head = ut_data;

        free_parent_cap(ut.cptr);

    }
    
    return 0;
}


int process_add_existing_ep(process_handle_t *handle,
                            seL4_CPtr existing_cap,
                            seL4_CapRights_t perms,
                            const char *conn_name)
{
    int error;

    /* TODO: Implement sync for init objects */
    if(!init_objects.initialized) {
        ZF_LOGW("Init objects (vka, vspace) have not been setup.\n"
                "Run init_process or init_root_task to complete.");
        return -1;
    }

    if(handle == NULL) {
        ZF_LOGE("Invalid process handle pointer passed to process_connect_ep.");
        return -2; /* TODO come up with error codes */
    }
    
    if(handle->running) {
        ZF_LOGW("Process is already running.");
        return -3; /* TODO come up with error codes */
    }

    error = copy_ep_to_proc(handle, existing_cap, perms, conn_name);
    if(error) {
        ZF_LOGE("Failed to copy ep to process");
        return -5;
    }

    return 0;
}


int process_connect_ep_self(process_handle_t *handle,
                            seL4_CapRights_t perms,
                            const char *conn_name,
                            seL4_CPtr *new_cap)
{
     int error;

     *new_cap = seL4_CapNull;

    /* TODO: Implement sync for init objects */
    if(!init_objects.initialized) {
        ZF_LOGW("Init objects (vka, vspace) have not been setup.\n"
                "Run init_process or init_root_task to complete.");
        return -1;
    }

    if(handle == NULL) {
        ZF_LOGE("Invalid process handle pointer passed to process_connect_ep.");
        return -2; /* TODO come up with error codes */
    }
    
    if(handle->running) {
        ZF_LOGW("Process is already running.");
        return -3; /* TODO come up with error codes */
    }


    vka_object_t ep;
    error = vka_alloc_endpoint(&init_objects.vka, &ep);
    if(error) {
        ZF_LOGE("Failed to allocate ep object.");
        return -4;
    }

    error = copy_ep_to_proc(handle, ep.cptr, perms, conn_name);
    if(error) {
        ZF_LOGE("Failed to copy ep to process");
        return -5;
    }

    *new_cap = ep.cptr;
    return 0;
}


int process_add_existing_notification(process_handle_t *handle,
                                      seL4_CPtr existing_cap,
                                      seL4_CapRights_t perms,
                                      const char *conn_name)
{
    int error;

    /* TODO: Implement sync for init objects */
    if(!init_objects.initialized) {
        ZF_LOGW("Init objects (vka, vspace) have not been setup.\n"
                "Run init_process or init_root_task to complete.");
        return -1;
    }

    if(handle == NULL) {
        ZF_LOGE("Invalid process handle pointer passed to process_connect_ep.");
        return -2; /* TODO come up with error codes */
    }
    
    if(handle->running) {
        ZF_LOGW("Process is already running.");
        return -3; /* TODO come up with error codes */
    }

    error = copy_notification_to_proc(handle, existing_cap, perms, conn_name);
    if(error) {
        ZF_LOGE("Failed to copy notification to process");
        return -5;
    }

    return 0;
}


int process_connect_notification_self(process_handle_t *handle,
                                      seL4_CapRights_t perms,
                                      const char *conn_name,
                                      seL4_CPtr *new_cap)
{
     int error;

     *new_cap = seL4_CapNull;

    /* TODO: Implement sync for init objects */
    if(!init_objects.initialized) {
        ZF_LOGW("Init objects (vka, vspace) have not been setup.\n"
                "Run init_process or init_root_task to complete.");
        return -1;
    }

    if(handle == NULL) {
        ZF_LOGE("Invalid process handle pointer passed to process_connect_ep.");
        return -2; /* TODO come up with error codes */
    }
    
    if(handle->running) {
        ZF_LOGW("Process is already running.");
        return -3; /* TODO come up with error codes */
    }

    vka_object_t ep;
    error = vka_alloc_notification(&init_objects.vka, &ep);
    if(error) {
        ZF_LOGE("Failed to allocate ep object.");
        return -4;
    }

    error = copy_notification_to_proc(handle, ep.cptr, perms, conn_name);
    if(error) {
        ZF_LOGE("Failed to copy ep to process 1");
        return -5;
    }

    *new_cap = ep.cptr;
    return 0;
}


int process_connect_shmem_self(process_handle_t *handle,
                               seL4_CapRights_t perms, 
                               seL4_Word num_pages,
                               const char *conn_name,
                               void **new_shmem)
{
    int error, i;

    /* TODO: Implement sync for init objects */
    if(!init_objects.initialized) {
        ZF_LOGW("Init objects (vka, vspace) have not been setup.\n"
                "Run init_process or init_root_task to complete.");
        return -1;
    }

    if(handle == NULL) {
        ZF_LOGE("Invalid process handle pointer passed to process_connect_shmem.");
        return -2; /* TODO come up with error codes */
    }

    if(handle->running) {
        ZF_LOGW("Process is already running.");
        return -3; /* TODO come up with error codes */
    }

    mmap_entry_attr_t attrs = mmap_attr_4k_data;
    attrs.readable = seL4_CapRights_get_capAllowRead(perms);
    attrs.writable = seL4_CapRights_get_capAllowWrite(perms);

    void *vaddr_child;

    seL4_CPtr *caps = malloc(sizeof(seL4_CPtr)*num_pages);
    if(caps == NULL) {
        ZF_LOGE("Failed to malloc temporary space for page caps");
        return -4;
    }

    /**
     * First map the pages into handle1
     */
    error = mmap_new_pages_custom(&handle->vspace,
                                  handle->page_dir.cptr,
                                  num_pages,
                                  &attrs,
                                  caps,
                                  &vaddr_child);
    if(error) {
        ZF_LOGE("Failed to map new pages into child");
        free(caps);
        return -5;
    }

    /**
     * We need to copy the caps to double map them.
     */
    for(int i = 0; i < num_pages; i++) {
        cspacepath_t path1, path2;
        vka_cspace_make_path(&init_objects.vka, caps[i], &path1);
        vka_cspace_alloc_path(&init_objects.vka, &path2);
        error = vka_cnode_copy(&path2, &path1, seL4_AllRights);
        if(error) {
            ZF_LOGE("Failed to copy cap for shared page.");
            free(caps);
            return -6;
        }
        free_parent_cap(caps[i]);
        caps[i] = path2.capPtr;
    }

    /**
     * Now we map the pages into our own space.
     */
    attrs.readable = 1;
    attrs.writable = 1;

    error = mmap_existing_pages_custom(&init_objects.vspace,
                                       init_objects.page_dir_cap,
                                       num_pages,
                                       &attrs,
                                       caps,
                                       new_shmem);
    if(error) {
        ZF_LOGE("Failed to share pages to second process");
        free(caps);
        return -7;
    }

    for(i = 0; i < num_pages; i++) {
        free_parent_cap(caps[i]);
    }
    free(caps);

    error = copy_shmem_to_proc(handle, vaddr_child, num_pages, conn_name);
    if(error) {
        ZF_LOGE("Failed to copy shemem data to proc");
        return -8;
    }

    return 0;
}


