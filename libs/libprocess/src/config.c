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


