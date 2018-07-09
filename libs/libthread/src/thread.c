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


const thread_attr_t thread_1mb_high_priority = {
    .stack_size_pages = 256,
    .priority = seL4_MaxPrio,
    .cpu_affinity = 0, /* TODO is this sane? */
};

thread_handle_t *thread_handle_create(const thread_attr_t *attr)
{
    UNUSED int error;
    /* TODO: implement sync */
    if(!init_objects.initialized) {
        ZF_LOGE("Init objects (vka, vspace) have not been setup.\n"
                "Run init_process or init_root_task to setup.");
        return NULL;
    }

    if(attr == NULL) {
        ZF_LOGE("Null thread attr passed into thread_handle_create");
        return NULL;
    }

    thread_handle_t *handle = thread_handle_create_custom(init_objects.cnode_cap,
                                                          0,
                                                          init_objects.fault_cap,
                                                          init_objects.page_dir_cap,
                                                          &init_objects.vspace,
                                                          attr);
    if(handle == NULL) {
        ZF_LOGE("Failed to create thread handle");
        return NULL;
    }

    static int tid_counter = 0;
    handle->thread_id = ++tid_counter;

#ifdef CONFIG_DEBUG_BUILD
    char *new_name;
    asprintf(&new_name, "%s-%i", init_objects.proc_name, handle->thread_id);
    seL4_DebugNameThread(handle->tcb.cptr, new_name);
    free(new_name);
#endif 

    return handle;
}

thread_handle_t *thread_handle_get_current()
{
    return (thread_handle_t*)init_get_thread_local_storage();
}


/**
 * @brief This function wraps around the given the user thread routine.
 *
 * This is the entry point of a new thread (besises the first proc thread).
 */
static void thread_init_routine(thread_handle_t *handle,
                                void *(*start_routine) (void *),
                                void *arg)
{
    if(start_routine == NULL) {
        ZF_LOGF("Invalid thread start function");
        return;
    }

    int error = init_set_thread_local_storage((void*)handle);
    if(error) {
        ZF_LOGF("Failed to set thread local storage");
    }
    
    /**
     * Take the return value of the thread and send it to the joining thread
     */
    seL4_Send(handle->join_ep.cptr,
              seL4_MessageInfo_new((seL4_Word)start_routine(arg), 0, 0, 0));
    
    ZF_LOGD("Thread finished executing");
    while(1); /* TODO */
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
    regs.x0 = (seL4_Word)handle;
    regs.x1 = (seL4_Word)start_routine;
    regs.x2 = (seL4_Word)arg;
#endif
#ifdef CONFIG_ARCH_AARCH32
    regs.r0 = (seL4_Word)handle;
    regs.r1 = (seL4_Word)start_routine;
    regs.r2 = (seL4_Word)arg;
#endif

/**
 * TODO: finish implementing x86!
 */
#ifdef CONFIG_ARCH_X86_64
    regs.rdi = (seL4_Word)arg;
    initial_stack_pointer -= sizeof(uintptr_t);
#endif
#ifdef CONFIG_ARCH_IA32
    void **dest = (void**)(initial_stack_pointer);
    *dest = arg;

#endif

    error = sel4utils_arch_init_context(thread_init_routine,
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


seL4_Word thread_get_id()
{
    thread_handle_t *handle = (thread_handle_t*)init_get_thread_local_storage();
    if(handle == NULL) {
        /* This is the initial thread case */
        return 0;
    }
    return handle->thread_id;
}

seL4_CPtr thread_get_sync_notification()
{
    thread_handle_t *handle = (thread_handle_t*)init_get_thread_local_storage();
    if(handle == NULL) {
        /* This is the initial thread case */
        return init_objects.sync_notification_cap;
    }
    return handle->sync_notification.cptr;
}



void *thread_join(thread_handle_t *handle)
{
    seL4_MessageInfo_t ret = seL4_Recv(handle->join_ep.cptr, NULL);
    return (void *)seL4_MessageInfo_get_label(ret);
}


thread_handle_t *thread_handle_create_custom(seL4_CPtr cnode,
                                seL4_Word cnode_root_data,
                                seL4_CPtr fault_ep,
                                seL4_CPtr page_dir,
                                vspace_t *vspace,
                                const thread_attr_t *attr)
{
    int error;

    /* TODO: implement sync for init objects */
    if(!init_objects.initialized) {
        ZF_LOGE("Init objects (vka, vspace) have not been setup.\n"
                "Run init_process or init_root_task to setup.");
        return NULL;
    }

    if(attr == NULL) {
        ZF_LOGE("Null thread attr passed into thread_handle_create_custom");
        return NULL;
    }

    thread_handle_t *handle = calloc(sizeof(thread_handle_t), 1);
    if(handle == NULL) {
        ZF_LOGE("Failed to malloc thread handle");
        return NULL;
    }
    
    /**
     * Create the tcb
     */
    error = vka_alloc_tcb(&init_objects.vka, &handle->tcb);
    if(error) {
        ZF_LOGW("Failed to allocate tcb.");
        return NULL;
    }
    
    /**
     * Create a notification object for libsync
     */
    error = vka_alloc_notification(&init_objects.vka, &handle->sync_notification);
    if(error) {
        ZF_LOGW("Failed to allocate notification ep.");
        return NULL;
    }
    
    /**
     * Create an endpoint for the parent thread to wait on the child.
     */
    error = vka_alloc_endpoint(&init_objects.vka, &handle->join_ep);
    if(error) {
        ZF_LOGW("Failed to allocate ep.");
        return NULL;
    }

    /**
     * Allocate the stack somewhere (reserves an extra guard page)
     */
    handle->stack_size_pages = attr->stack_size_pages;
    error = mmap_new_stack_custom(vspace,
                                  page_dir,
                                  handle->stack_size_pages,
                                  &handle->stack_vaddr);
    if(error) {
        ZF_LOGW("Failed to allocate stack");
        return NULL;
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
        return NULL;
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
        return NULL;
    }                 

    error = seL4_TCB_SetPriority(handle->tcb.cptr, init_objects.tcb_cap, attr->priority);
    if(error) {
        ZF_LOGW("Failed to set priority");
    }
   
#ifdef ENABLE_SMP_SUPPORT 
    error = seL4_TCB_SetAffinity(handle->tcb.cptr, attr->cpu_affinity);
    if(error) {
        ZF_LOGW("Failed to set affinity");
    }
#endif

    return handle;

}


