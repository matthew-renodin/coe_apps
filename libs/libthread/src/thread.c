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



int thread_handle_create_custom(seL4_CPtr fault_ep,
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
     * Allocate the stack somewhere
     */
    reservation_t res = vspace_reserve_range(vspace,
                                             stack_size_pages * PAGE_SIZE_4K,
                                             seL4_AllRights,
                                             1,
                                             &handle->stack_vaddr);
    if(res.res == 0) {
        ZF_LOGW("Failed to reserve stack space");
        return -3;
    }

     error = vspace_new_pages_at_vaddr(vspace,
                                       handle->stack_vaddr,
                                       stack_size_pages,
                                       PAGE_BITS_4K,
                                       res);
    if(error) {
        ZF_LOGW("Failed to map stack space");
        return error;
    }

    return 0;

}


