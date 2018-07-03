/**
 * @file thread.c
 * @brief Core implementation of libthread
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>

#include <sel4/sel4.h>
#include <vka/vka.h>
#include <vspace/vspace.h>
#include <utils/util.h>
#include <sel4utils/helpers.h>

#include <thread/thread.h>
#include <mmap/mmap.h>
#include <init/init.h>

int thread_handle_create(seL4_Word stack_size_pages,
                         seL4_Word priority,
                         seL4_Word cpu_affinity,
                         thread_handle_t *handle)
{
    int error;
    /* TODO: implement sync */
    if(!init_objects.initialized) {
        ZF_LOGE("Init objects (vka, vspace) have not been setup.\n"
                "Run init_process or init_root_task to setup.");
        return -1;
    }

    if(handle == NULL) {
        ZF_LOGE("Null thread handle passed into thread_handle_create");
        return -2;
    }

    error = thread_handle_create_custom(init_objects.cnode_cap,
                                        0,
                                        init_objects.fault_cap,
                                        init_objects.page_dir_cap,
                                        &init_objects.vspace,
                                        stack_size_pages,
                                        priority,
                                        cpu_affinity,
                                        handle);
    if(error) {
        ZF_LOGE("Failed to create thread handle");
        return -3;
    }

#ifdef CONFIG_DEBUG_BUILD
    static int counter = 0;
    char *new_name;
    asprintf(&new_name, "%s-%i", init_objects.init_data->proc_name, ++counter);
    seL4_DebugNameThread(handle->tcb.cptr, new_name);
    free(new_name);
#endif 

    return 0;
}

int thread_start(thread_handle_t *handle, void *(*start_routine) (void *), void *arg) {
    int error;
    seL4_UserContext regs = {0};

    if(handle == NULL) {
        ZF_LOGE("Null thread handle passed into thread_start");
        return -1;
    }

    /**
     * ARM requires 8-byte alignment
     */
    uintptr_t initial_stack_pointer = (uintptr_t)handle->stack_vaddr - sizeof(seL4_Word);

    uintptr_t unaligned_future_stack_pointer = initial_stack_pointer - sizeof(void *);
    uintptr_t aligned_future_stack_pointer = ALIGN_DOWN(unaligned_future_stack_pointer, STACK_CALL_ALIGNMENT);
    ptrdiff_t offset =  unaligned_future_stack_pointer - aligned_future_stack_pointer;
    initial_stack_pointer -= offset;

    initial_stack_pointer -= sizeof(void *);

#ifdef CONFIG_ARCH_AARCH64
    regs.x0 = (seL4_Word)arg;
#endif
#ifdef CONFIG_ARCH_AARCH32
    regs.r0 = (seL4_Word)arg;
#endif

/**
 * TODO: TEST x86!
 */
#ifdef CONFIG_ARCH_X86_64
    regs.rdi = (seL4_Word)arg;
    initial_stack_pointer -= sizeof(uintptr_t);
#endif
#ifdef CONFIG_ARCH_IA32
    void **dest = (void**)(initial_stack_pointer);
    *dest = arg;

#endif

    error = sel4utils_arch_init_context(start_routine,
                                        (void*)initial_stack_pointer,
                                        &regs);
    if(error) {
        ZF_LOGE("Failed to initialize thread registers");
        return -2;
    }

    error = seL4_TCB_WriteRegisters(handle->tcb.cptr, 1, 0, sizeof(regs)/sizeof(seL4_Word), &regs);
    if(error) {
        ZF_LOGE("Failed to write tcb registers");
        return -3;
    }

    return 0;
}

seL4_Word thread_get_id() {
    return 0;
}



int thread_handle_create_custom(seL4_CPtr cnode,
                                seL4_Word cnode_root_data,
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
    error = mmap_new_stack_custom(vspace,
                                  page_dir,
                                  stack_size_pages,
                                  &handle->stack_vaddr);
    if(error) {
        ZF_LOGW("Failed to allocate stack");
        return -3;
    }

    /**
     * Allocate an IPC buffer
     */
    error = mmap_new_pages_custom(vspace,
                                  page_dir,
                                  1,
                                  &mmap_attr_4k_data,
                                  &handle->ipc_buffer_cap,
                                  &handle->ipc_buffer_vaddr);
    if(error) {
        ZF_LOGW("Failed to allocate ipc buffer");
        return -4;
    }

    /**
     * Configure our tcb with the new resources
     */
    error = seL4_TCB_Configure(handle->tcb.cptr,
                               fault_ep,
                               cnode,
                               cnode_root_data,
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
   
#ifdef ENABLE_SMP_SUPPORT 
    error = seL4_TCB_SetAffinity(handle->tcb.cptr, cpu_affinity);
    if(error) {
        ZF_LOGW("Failed to set affinity");
    }
#endif

    return 0;

}


