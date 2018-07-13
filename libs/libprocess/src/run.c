/**
 * @file process.c
 * @brief Implementation of process_run
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


int process_run(process_handle_t *handle, int argc, char *argv[])
{
    int error;

    /* TODO: Implement sync for init objects */
    if(!init_check_initialized()) {
        ZF_LOGW("Init objects (vka, vspace) have not been setup.\n"
                "Run init_process or init_root_task to complete.");
        return -1;
    }

    if(handle == NULL || handle->entry_point == NULL || handle->main_thread->stack_vaddr == NULL) {
        ZF_LOGE("Invalid process handle pointer passed to process_run.");
        return -2; /* TODO come up with error codes */
    }

    if(handle->running) {
        ZF_LOGW("Process is already running.");
        return -3; /* TODO come up with error codes */
    }

    handle->init_data.cnode_next_free = handle->cnode_next_free;


    /**
     * Copy the init data into the child memory space
     */
    seL4_Word raw_size = init_data__get_packed_size(&handle->init_data);
    seL4_Word init_data_len = ROUND_UP(raw_size, PAGE_SIZE_4K);
    ZF_LOGV("Starting process with init data size: %lu", raw_size);

    void *init_data_vaddr;
    reservation_t res; /* TODO: track this reservation to free later */
    error = mmap_new_pages_custom(&handle->vspace,
                                  handle->page_dir.cptr,
                                  init_data_len / PAGE_SIZE_4K,
                                  &mmap_attr_4k_data,
                                  NULL,
                                  &init_data_vaddr,
                                  &res);
    if(error) {
        ZF_LOGE("Failed to allocate space for the init data");
        return -5;
    }

    
    void * packed_init_data = vspace_share_mem(&handle->vspace,
                                               &init_objects.vspace,
                                               init_data_vaddr,
                                               init_data_len / PAGE_SIZE_4K,
                                               PAGE_BITS_4K,
                                               seL4_AllRights,
                                               1); /* should this be cacheable? */
    if(packed_init_data == NULL) {
        ZF_LOGE("Failed to share init_data.");
        return -6;
    }

    /**
     * Then write the packed data
     */
    init_data__pack(&handle->init_data, packed_init_data);

    /**
     * We don't need the data in our address space anymore, unmap
     */
    sel4utils_unmap_pages(&init_objects.vspace,
                          packed_init_data,
                          init_data_len / PAGE_SIZE_4K,
                          PAGE_BITS_4K,
                          &init_objects.vka);
    /**
     * TODO: free all the init data
     */

    
    /**
     * Our child process expects the stack to look like this:
     * 
     * +------------------+
     * | argc             |
     * +------------------+
     * | argv *           |
     * +------------------+
     * | env *            |
     * +------------------+
     * | aux *            |
     * +------------------+
     *  
     * We need to pass a few environment variables to the child to
     * help it find/unpack its init data.
     */
    AUTOFREE char *heap_addr_env;
    error = asprintf(&heap_addr_env,
                     "HEAP_ADDR=0x%"PRIxPTR"",
                     handle->heap_vaddr);
    if (error == -1) {
        return -1;
    }

    AUTOFREE char *heap_size_env;
    error = asprintf(&heap_size_env,
                     "HEAP_SIZE=%lu",
                     (long unsigned)handle->attrs.heap_size_pages * PAGE_SIZE_4K);
    if (error == -1) {
        return -1;
    }

    AUTOFREE char *init_data_addr_env;
    error = asprintf(&init_data_addr_env,
                     "INIT_DATA_ADDR=0x%"PRIxPTR"",
                     init_data_vaddr);
    if (error == -1) {
        return -1;
    }

    AUTOFREE char *init_data_size_env;
    error = asprintf(&init_data_size_env,
                     "INIT_DATA_SIZE=%lu",
                     (long unsigned)raw_size);
    if (error == -1) {
        return -1;
    }


    char *envp[] = {heap_addr_env, heap_size_env, init_data_addr_env, init_data_size_env};
    int envc = sizeof(envp)/sizeof(envp[0]);

    uintptr_t initial_stack_pointer = (uintptr_t)handle->main_thread->stack_vaddr - sizeof(seL4_Word); 
    /**
     * Copy the elf headers
     */
    uintptr_t at_phdr;
    error = sel4utils_stack_write(&init_objects.vspace,
                                  &handle->vspace,
                                  &init_objects.vka,
                                  handle->elf_phdrs,
                                  handle->num_elf_phdrs * sizeof(Elf_Phdr),
                                  &initial_stack_pointer);
    if (error) {
        ZF_LOGE("Failed to write the elf headers to the stack.");
        return -1;
    }
    at_phdr = initial_stack_pointer;

    /**
     * initialize aux vectors
     */
    int auxc = 5;
    Elf_auxv_t auxv[5];

    /* System page size */
    auxv[0].a_type = AT_PAGESZ;    
    auxv[0].a_un.a_val = PAGE_SIZE_4K;

    /* Program headers for program */
    auxv[1].a_type = AT_PHDR;       
    auxv[1].a_un.a_val = at_phdr;

    /* Number of program headers */
    auxv[2].a_type = AT_PHNUM;      
    auxv[2].a_un.a_val = handle->num_elf_phdrs;

    /* Size of program header entry */
    auxv[3].a_type = AT_PHENT;      
    auxv[3].a_un.a_val = sizeof(Elf_Phdr);

    /* Pointer to the global system page used for system calls and other nice things.  */
    auxv[4].a_type = AT_SYSINFO;
    auxv[4].a_un.a_val = handle->sysinfo;

    seL4_UserContext context = {0};
    uintptr_t dest_argv[argc];
    uintptr_t dest_envp[envc];

    /* Copy the argv onto the stack. */
    error = sel4utils_stack_copy_args(&init_objects.vspace,
                                      &handle->vspace,
                                      &init_objects.vka,
                                      argc,
                                      argv,
                                      dest_argv,
                                      &initial_stack_pointer);
    if (error) {
        ZF_LOGE("Failed to copy argv onto the stack.");
        return error;
    }

    /* Copy the env onto the stack */
    error = sel4utils_stack_copy_args(&init_objects.vspace,
                                      &handle->vspace,
                                      &init_objects.vka,
                                      envc,
                                      envp,
                                      dest_envp,
                                      &initial_stack_pointer);
    if (error) {
        ZF_LOGE("Failed to copy env onto the stack.");
        return error;
    }

    /**
     * We need to make sure the stack is aligned to a double word boundary
     * after we push on everything else below this point.
     * First, work out how much we are going to push
     */
    size_t to_push = 5 * sizeof(seL4_Word) +    /* constants */
                    sizeof(auxv[0]) * auxc +    /* aux */
                    sizeof(dest_argv) +         /* args */
                    sizeof(dest_envp);          /* env */
    uintptr_t hypothetical_stack_pointer = initial_stack_pointer - to_push;
    uintptr_t rounded_stack_pointer = ALIGN_DOWN(hypothetical_stack_pointer, STACK_CALL_ALIGNMENT);
    ptrdiff_t stack_rounding = hypothetical_stack_pointer - rounded_stack_pointer;
    initial_stack_pointer -= stack_rounding;


    /**
     * Construct initial stack frame.
     * Working from top to bottom.
     */

    /* Null terminate aux */
    error = sel4utils_stack_write_constant(&init_objects.vspace,
                                           &handle->vspace,
                                           &init_objects.vka,
                                           0,
                                           &initial_stack_pointer);
    if (error) {
        ZF_LOGE("Failed to arugments to new process stack");
        return error;
    }
    error = sel4utils_stack_write_constant(&init_objects.vspace,
                                           &handle->vspace,
                                           &init_objects.vka,
                                           0,
                                           &initial_stack_pointer);
    if (error) {
        ZF_LOGE("Failed to arugments to new process stack");
        return error;
    }

    /* write aux */
    error = sel4utils_stack_write(&init_objects.vspace,
                                  &handle->vspace,
                                  &init_objects.vka,
                                  auxv,
                                  sizeof(auxv[0]) * auxc,
                                  &initial_stack_pointer);
    if (error) {
        ZF_LOGE("Failed to arugments to new process stack");
        return error;
    }

    /* Null terminate environment */
    error = sel4utils_stack_write_constant(&init_objects.vspace,
                                           &handle->vspace,
                                           &init_objects.vka,
                                           0,
                                           &initial_stack_pointer);
    if (error) {
        ZF_LOGE("Failed to arugments to new process stack");
        return error;
    }

    /* write environment */
    error = sel4utils_stack_write(&init_objects.vspace,
                                  &handle->vspace,
                                  &init_objects.vka,
                                  dest_envp,
                                  sizeof(dest_envp),
                                  &initial_stack_pointer);
    if (error) {
        ZF_LOGE("Failed to arugments to new process stack");
        return error;
    }

    /* Null terminate arguments */
    error = sel4utils_stack_write_constant(&init_objects.vspace,
                                           &handle->vspace,
                                           &init_objects.vka,
                                           0,
                                           &initial_stack_pointer);
    if (error) {
        ZF_LOGE("Failed to arugments to new process stack");
        return error;
    }

    /* write arguments */
    error = sel4utils_stack_write(&init_objects.vspace,
                                  &handle->vspace,
                                  &init_objects.vka,
                                  dest_argv,
                                  sizeof(dest_argv),
                                  &initial_stack_pointer);
    if (error) {
        ZF_LOGE("Failed to arugments to new process stack");
        return error;
    }

    /* Push argument count */
    error = sel4utils_stack_write_constant(&init_objects.vspace,
                                           &handle->vspace,
                                           &init_objects.vka,
                                           argc,
                                           &initial_stack_pointer);
    if (error) {
        ZF_LOGE("Failed to arugments to new process stack");
        return error;
    }

    assert(initial_stack_pointer % (2 * sizeof(seL4_Word)) == 0);

    /**
     * Setup the initial register values for our process
     */
    error = sel4utils_arch_init_context(handle->entry_point, 
                                        (void *) initial_stack_pointer,
                                        &context);
    if(error) {
        ZF_LOGE("Failed to initialize process context");
        return error;
    }

    /**
     * Start the thread running.
     */
    error = seL4_TCB_WriteRegisters(handle->main_thread->tcb.cptr,
                                    1, /* Resume */
                                    0, /* Arch flags */
                                    sizeof(context)/sizeof(seL4_Word),
                                    &context);
    if(error) {
        ZF_LOGE("Failed to write registers for new process");
        return error;
    }

    /**
     * Prevent future changes to the handle
     */
    handle->running = 1; 

   return 0; 
}





