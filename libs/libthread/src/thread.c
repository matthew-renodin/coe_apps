/**
 * @file thread.c
 * @brief Core implementation of libthread
 */

#include <sel4/sel4.h>
#include <vka/vka.h>
#include <vspace/vspace.h>
#include <utils/util.h>

#include <thread/thread.h>
#include <init/init.h>

int thread_handle_create(seL4_Word stack_size_pages,
                         seL4_Word priority,
                         seL4_Word cpu_affinity,
                         thread_handle_t *handle)
{
    return 0;
}

int thread_start(thread_handle_t *handle, void *(*start_routine) (void *), void *arg) {
    return 0;
}

seL4_Word thread_get_id() {
    return 0;
}



int thread_handle_create_custom(seL4_CPtr cnode,
                                seL4_CPtr fault_ep,
                                seL4_CPtr page_dir,
                                vspace_t *vspace,
                                seL4_Word stack_size_pages,
                                seL4_Word priority,
                                seL4_Word cpu_affinity,
                                thread_handle_t *handle)
{
    int error;

    /* TODO: implement sync for init objects */
    if(!init_objects.initialized) return -1;

    /* TODO come up with error codes */
    if(handle == NULL) return -2;
    
    /**
     * Create the tcb
     */
    error = vka_alloc_tcb(&init_objects.vka, &handle->tcb);
    if(error) {
        ZF_LOGW("Failed to allocate tcb.");
        return error;
    }
    
    /**
     * Allocate the stack somewhere (reserves an extra guard page)
     */
    handle->stack_size_pages = stack_size_pages;
    handle->stack_vaddr = vspace_new_sized_stack(vspace, stack_size_pages);
    if(handle->stack_vaddr == NULL) {
        ZF_LOGW("Failed to allocate stack");
        return -3;
    }

    /**
     * Allocate an IPC buffer
     */
    handle->ipc_buffer_vaddr = vspace_new_ipc_buffer(vspace, &handle->ipc_buffer_cap);
    if(handle->ipc_buffer_vaddr == NULL) {
        ZF_LOGW("Failed to allocate ipc buffer");
        return -4;
    }

    /**
     * Configure our tcb with the new resources
     */
    error = seL4_TCB_Configure(handle->tcb.cptr,
                               fault_ep,
                               cnode,
                               0,
                               page_dir,
                               0,
                               (seL4_Word)handle->ipc_buffer_vaddr,
                               handle->ipc_buffer_cap);
    if(error) {
        ZF_LOGW("Failed to configure tcb");
        return -5;
    }                 

    error = seL4_TCB_SetPriority(handle->tcb.cptr, init_objects.tcb_cap, priority);
    if(error) {
        ZF_LOGW("Failed to set priority");
    }
    
    error = seL4_TCB_SetAffinity(handle->tcb.cptr, cpu_affinity);
    if(error) {
        ZF_LOGW("Failed to set affinity");
    }

    return 0;

}


