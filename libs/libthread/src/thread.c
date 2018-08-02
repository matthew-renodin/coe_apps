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

#include <thread/sync.h>

const thread_attr_t thread_1mb_high_priority = {
    .stack_size_pages = 256,
    .priority = seL4_MaxPrio,
    .cpu_affinity = 0, /* TODO is this sane? */
};

int thread_lib_lock_initialized = 0;
mutex_t thread_lib_lock = {0};

static inline bool is_current_thread(thread_handle_t *handle) {
    return handle == (thread_handle_t*)init_get_thread_local_storage();
}


thread_handle_t *thread_handle_create(const thread_attr_t *attr)
{
    libthread_prologue(thread_handle_t *, NULL);

    libthread_check_initialized(NULL);
    
    libthread_guard(attr == NULL, NULL, libthread_epilogue,
                    "Null thread attr passed into thread_handle_create");

    thread_handle_t *handle = thread_handle_create_custom(init_objects.cnode_cap,
                                                          0,
                                                          init_objects.fault_cap,
                                                          init_objects.page_dir_cap,
                                                          &init_objects.vspace,
                                                          attr);
    libthread_guard(handle == NULL, NULL, libthread_epilogue,
                    "Failed to create thread handle");

    static int tid_counter = 0;
    handle->thread_id = ++tid_counter;

#ifdef CONFIG_DEBUG_BUILD
    char *new_name;
    asprintf(&new_name, "%s-%i", init_objects.proc_name, handle->thread_id);
    seL4_DebugNameThread(handle->tcb.cptr, new_name);
    free(new_name);
#endif 

    libthread_return_value(handle);
    libthread_epilogue();
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
    /* Check this before we even go for the lock */
    if(start_routine == NULL) {
        ZF_LOGF("Invalid thread start function");
        return;
    }

    int error = init_set_thread_local_storage((void*)handle);
    if(error) {
        ZF_LOGF("Failed to set thread local storage");
    }

    libthread_lock_acquire();

    libthread_condition_variable_init(handle);

    libthread_lock_release();
    
    /**
     * Take the return value of the thread and send it to the joining thread
     */
    handle->returned_value = start_routine(arg);
    
    libthread_lock_acquire();
    ZF_LOGD("Thread finished executing");
    thread_state_t expected = THREAD_RUNNING;
    atomic_compare_exchange(&handle->state, &expected, THREAD_DESTROYED);
    cond_signalAll(&handle->join_condition);
    libthread_lock_release();
    
    while(1) {
        /* TODO */
        seL4_Sleep(5000);
        ZF_LOGD("Thread %lu finished, yet undestroyed", (long unsigned)handle->thread_id);
    }
}


int thread_start(thread_handle_t *handle, void *(*start_routine) (void *), void *arg) {
    libthread_prologue(int, 0);
    seL4_UserContext regs = {0};

    libthread_guard(handle == NULL, -1, libthread_epilogue,
                    "Null thread handle passed into thread_start");

    libthread_guard(start_routine == NULL, -2, libthread_epilogue,
                    "Null function pointer passed to thread_start");

    libthread_guard(handle->state != THREAD_INIT, -3, libthread_epilogue,
                    "Cannot start an already started thread");

    handle->state = THREAD_RUNNING;

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

    libthread_set_status(sel4utils_arch_init_context(thread_init_routine,
                                                     (void*)initial_stack_pointer,
                                                     &regs));
    libthread_guard(libthread_get_status(), -4, libthread_epilogue,
                    "Failed to initialize thread registers");

    libthread_set_status(seL4_TCB_WriteRegisters(handle->tcb.cptr, 1, 0, sizeof(regs)/sizeof(seL4_Word), &regs));
    libthread_guard(libthread_get_status(), -5, libthread_epilogue,
                    "Failed to write tcb registers");

    libthread_return_success();
    libthread_epilogue();
}


int thread_get_id()
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
    ZF_LOGF_IF(holding_libthread_lock(), 
               "Trying to join while already holding libthread lock "
               "will cause a deadlock situation in libthread. Abort.\n");
    
    libthread_prologue(void *, NULL);
    libthread_guard(handle == NULL, NULL, libthread_epilogue, "Null thread handle passed");

    libthread_condition_variable_init(handle);
    if(handle->state != THREAD_DESTROYED ) {
        cond_wait(&handle->join_condition);
        libthread_guard(handle == NULL, NULL, libthread_epilogue,
                        "Thread handle freed before return");
    }
    
    /* Save off returned value before letting go of lock */
    libthread_set_status(handle->returned_value);
    libthread_return_value(libthread_get_status());
    
    libthread_epilogue();
}

/**
 * Convenience functions to unmap/free the stack and IPC Buffer
 * Assumes that libthread lock is held, handle is not null,
 *  init_objects are properly initialized, and that 
 *  the stack/buffer to be freed is valid
 */
static inline void thread_unmap_stack_unsafe(thread_handle_t *handle, vspace_t* vspace) {
    void *stack_bottom = (void*)((uintptr_t)handle->stack_vaddr -
                                 (handle->stack_size_pages << PAGE_BITS_4K));
    vspace_unmap_pages(vspace,
                       stack_bottom,
                       handle->stack_size_pages,
                       PAGE_BITS_4K,
                       &init_objects.vka);
    vspace_free_reservation(vspace, handle->stack_res);
}


static inline void thread_unmap_ipc_buffer_unsafe(thread_handle_t *handle, vspace_t* vspace) {
    vspace_unmap_pages(vspace,
                       handle->ipc_buffer_vaddr,
                       1,
                       PAGE_BITS_4K,
                       &init_objects.vka);

    vspace_free_reservation(vspace, handle->ipc_buffer_res);
}


thread_handle_t *thread_handle_create_custom(seL4_CPtr cnode,
                                             seL4_Word cnode_root_data,
                                             seL4_CPtr fault_ep,
                                             seL4_CPtr page_dir,
                                             vspace_t *vspace,
                                             const thread_attr_t *attr)
{
    libthread_prologue(thread_handle_t *, NULL);
    int error;

    libthread_check_initialized(NULL);

    libthread_guard(attr == NULL, NULL, libthread_epilogue,
                    "Null thread attr passed into thread_handle_create_custom");

    thread_handle_t *handle = calloc(sizeof(thread_handle_t), 1);
    libthread_guard(handle == NULL, NULL, libthread_epilogue,
                    "Failed to malloc thread handle");
    
    handle->state = THREAD_INIT;

    /**
     * Create the tcb
     */
    error = vka_alloc_tcb(&init_objects.vka, &handle->tcb);
    libthread_guard(error, NULL, tcb_fail,
                    "Failed to allocate tcb.");
    
    /**
     * Create a notification object for libsync
     */
    error = vka_alloc_notification(&init_objects.vka, &handle->sync_notification);
    libthread_guard(error, NULL, sync_fail,
                    "Failed to allocate notification ep.");
    
    /**
     * Create an endpoint for the parent thread to wait on the child.
     */
    error = vka_alloc_notification(&init_objects.vka, &handle->join_notification);
    libthread_guard(error, NULL, join_fail,
                    "Failed to allocate notification ep.");

    /**
     * Allocate the stack somewhere (reserves an extra guard page)
     */
    handle->stack_size_pages = attr->stack_size_pages;
    error = mmap_new_stack_custom(vspace,
                                  page_dir,
                                  handle->stack_size_pages,
                                  &handle->stack_vaddr,
                                  &handle->stack_res);
    libthread_guard(error, NULL, stack_fail,
                    "Failed to allocate stack");

    /**
     * Allocate an IPC buffer
     */
    error = mmap_new_pages_custom(vspace,
                                  page_dir,
                                  1,
                                  &mmap_attr_4k_data,
                                  &handle->ipc_buffer_cap,
                                  &handle->ipc_buffer_vaddr,
                                  &handle->ipc_buffer_res);
    libthread_guard(error, NULL, ipc_fail,
                    "Failed to allocate ipc buffer");

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
    libthread_guard(error, NULL, tcb_configure_fail,
                    "Failed to configure tcb");           

    error = seL4_TCB_SetPriority(handle->tcb.cptr, init_objects.tcb_cap, attr->priority);
    ZF_LOGW_IF(error, "Failed to set priority");
   
#if CONFIG_MAX_NUM_NODES > 1
    error = seL4_TCB_SetAffinity(handle->tcb.cptr, attr->cpu_affinity);
    ZF_LOGW_IF(error, "Failed to set affinity");
#endif
    libthread_return_value(handle);
    tcb_configure_fail:
        thread_unmap_ipc_buffer_unsafe(handle, vspace);
    ipc_fail:
        thread_unmap_stack_unsafe(handle, vspace);
    stack_fail:
        vka_free_object(&init_objects.vka, &handle->join_notification);
    join_fail:
        vka_free_object(&init_objects.vka, &handle->sync_notification);
    sync_fail:
        vka_free_object(&init_objects.vka, &handle->tcb);
    tcb_fail:
        free(handle);
    libthread_epilogue();
}


int thread_destroy_free_handle_custom(thread_handle_t **handle_ref,
                                      vspace_t *vspace)
{
    libthread_prologue(int, 0);
    UNUSED int error;

    libthread_check_initialized(-1);

    libthread_guard(handle_ref == NULL || *handle_ref == NULL,
                    -2, libthread_epilogue,
                    "Null thread handle passed");

    thread_handle_t *handle = *handle_ref;

    seL4_TCB_Suspend(handle->tcb.cptr);

    vka_free_object(&init_objects.vka, &handle->tcb);
    vka_free_object(&init_objects.vka, &handle->sync_notification);

    thread_unmap_stack_unsafe(handle, vspace);
    thread_unmap_ipc_buffer_unsafe(handle, vspace);

    /**
     * Wake any threads wanting to join 
     *  and prevent new ones from joining
     */
    handle->state = THREAD_DESTROYED;
    libthread_condition_variable_init(handle);
    cond_signalAll(&handle->join_condition);

    vka_free_object(&init_objects.vka, &handle->join_notification);    
    free(handle);
    
    /**
     * Help prevent double free/use after free bugs.
     */
    *handle_ref = NULL;

    libthread_return_success();
    libthread_epilogue();
}


int thread_destroy_free_handle(thread_handle_t **handle_ref) {
    libthread_prologue(int, 0);

    libthread_guard(handle_ref == NULL || *handle_ref == NULL,
                    -2, libthread_epilogue,
                    "Null thread handle passed");

    libthread_guard(is_current_thread(*handle_ref),
                    -3, libthread_epilogue,
                    "Cannot destroy currently executing thread");
    
    libthread_set_status(thread_destroy_free_handle_custom(handle_ref, &init_objects.vspace));
    
    libthread_return_value(libthread_get_status());
    libthread_epilogue();
}

