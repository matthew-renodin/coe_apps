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
#include <process/sync.h>

#define FREE_INIT_LIST(TYPE, LIST) do { \
    TYPE *lst = LIST; \
    while(lst != NULL) { \
        TYPE *tmp = lst; \
        lst = lst->next; \
        free(tmp); \
    } \
} while(0) 

static void free_init_data(InitData* data) {
    FREE_INIT_LIST(UntypedData, data->untyped_list_head);
    FREE_INIT_LIST(EndpointData, data->ep_list_head);
    FREE_INIT_LIST(EndpointData, data->notification_list_head);
    FREE_INIT_LIST(SharedMemoryData, data->shmem_list_head);
    FREE_INIT_LIST(IrqData, data->irq_list_head);

    {
        DeviceMemoryData *lst = data->devmem_list_head;
        while(lst != NULL) {
            if(lst->caps32 != NULL) free(lst->caps32);
            if(lst->caps64 != NULL) free(lst->caps64);
            DeviceMemoryData *tmp = lst;
            lst = lst->next;
            free(tmp);
        }
    }

    init_data__init(data);
}

static inline int 
threadsafe_stack_write_constant(lockvspace_t *lockvspace, vspace_t *current_vspace, vspace_t *target_vspace,
                                vka_t *vka, long value, uintptr_t *initial_stack_pointer){
    int error = 0;
    lockvspace_lock(current_vspace, lockvspace);
    error = sel4utils_stack_write_constant(&lockvspace->parent_vspace, target_vspace, vka, value, initial_stack_pointer);
    lockvspace_unlock(current_vspace, lockvspace);
    return error;
}

static inline int
threadsafe_stack_write(lockvspace_t *lockvspace, vspace_t *current_vspace, vspace_t *target_vspace,
                       vka_t *vka, void *buf, size_t len, uintptr_t *stack_top) {
    int error = 0;
    lockvspace_lock(current_vspace, lockvspace);
    error = sel4utils_stack_write(&lockvspace->parent_vspace, target_vspace, vka, buf, len, stack_top);
    lockvspace_unlock(current_vspace, lockvspace);
    return error;
}

static inline int
threadsafe_stack_copy_args(lockvspace_t *lockvspace, vspace_t *current_vspace, vspace_t *target_vspace,
                           vka_t *vka, int argc, char *argv[], uintptr_t *dest_argv, uintptr_t *initial_stack_pointer){
    int error = 0;
    lockvspace_lock(current_vspace, lockvspace);
    error = sel4utils_stack_copy_args(&lockvspace->parent_vspace, target_vspace, vka, argc, argv, dest_argv, initial_stack_pointer);
    lockvspace_unlock(current_vspace, lockvspace);
    return error;
}

int process_run(process_handle_t *handle, int argc, char *argv[])
{
    libprocess_prologue();
    UNUSED int error;

    libprocess_check_initialized();

    libprocess_check_arg(handle);
    libprocess_check_arg(handle->entry_point);
    libprocess_check_arg(handle->main_thread->stack_vaddr);

    libprocess_check_state(handle, PROCESS_INIT);
    handle->state = PROCESS_RUNNING;

    handle->init_data.cnode_next_free = handle->cnode_next_free;


    /**
     * Copy the init data into the child memory space
     */
    seL4_Word raw_size = init_data__get_packed_size(&handle->init_data);
    seL4_Word init_data_len = ROUND_UP(raw_size, PAGE_SIZE_4K);
    ZF_LOGV("Starting process with init data size: %lu", (long unsigned)raw_size);

    void *init_data_vaddr;
    reservation_t res; 
    libprocess_set_status(mmap_new_pages_custom(&handle->vspace,
                                                handle->page_dir.cptr,
                                                init_data_len / PAGE_SIZE_4K,
                                                &mmap_attr_4k_data,
                                                NULL,
                                                &init_data_vaddr,
                                                &res));
    libprocess_guard(libprocess_get_status(), -6, libprocess_epilogue, 
                     "Failed to allocate space for the init data");

    
    void * packed_init_data = vspace_share_mem(&handle->vspace,
                                               &init_objects.vspace,
                                               init_data_vaddr,
                                               init_data_len / PAGE_SIZE_4K,
                                               PAGE_BITS_4K,
                                               seL4_AllRights,
                                               1); /* should this be cacheable? */
    libprocess_guard(packed_init_data == NULL, -6, libprocess_epilogue, 
                     "Failed to share init_data.");

    /**
     * Then write the packed data
     */
    init_data__pack(&handle->init_data, packed_init_data);

    /**
     * We don't need the data in our address space anymore, unmap
     */
    lockvspace_lock(&init_objects.vspace, &init_objects.lockvspace);
    sel4utils_unmap_pages(&init_objects.lockvspace.parent_vspace,
                          packed_init_data,
                          init_data_len / PAGE_SIZE_4K,
                          PAGE_BITS_4K,
                          &init_objects.vka);
    lockvspace_unlock(&init_objects.vspace, &init_objects.lockvspace);

    /**
     * free all the init data);
     */
    free_init_data(&handle->init_data);
    
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
    libprocess_set_status(asprintf(&heap_addr_env,
                                   "HEAP_ADDR=0x%"PRIxPTR"",
                                   handle->heap_vaddr));
    libprocess_guard(libprocess_get_status() == -1, -6, libprocess_epilogue, 
                     "Failed to allocate environment variable for child");

    AUTOFREE char *heap_size_env;
    libprocess_set_status(asprintf(&heap_size_env,
                          "HEAP_SIZE=%lu",
                          (long unsigned)handle->attrs.heap_size_pages * PAGE_SIZE_4K));
    libprocess_guard(libprocess_get_status() == -1, -6, libprocess_epilogue, 
                     "Failed to allocate environment variable for child");

    AUTOFREE char *init_data_addr_env;
    libprocess_set_status(asprintf(&init_data_addr_env,
                                   "INIT_DATA_ADDR=0x%"PRIxPTR"",
                                   init_data_vaddr));
    libprocess_guard(libprocess_get_status() == -1, -6, libprocess_epilogue, 
                     "Failed to allocate environment variable for child");

    AUTOFREE char *init_data_size_env;
    libprocess_set_status(asprintf(&init_data_size_env,
                                   "INIT_DATA_SIZE=%lu",
                                   (long unsigned)raw_size));
    libprocess_guard(libprocess_get_status() == -1, -6, libprocess_epilogue, 
                     "Failed to allocate environment variable for child");


    char *envp[] = {heap_addr_env, heap_size_env, init_data_addr_env, init_data_size_env};
    int envc = sizeof(envp)/sizeof(envp[0]);

    uintptr_t initial_stack_pointer = (uintptr_t)handle->main_thread->stack_vaddr - sizeof(seL4_Word); 
    /**
     * Copy the elf headers
     */
    uintptr_t at_phdr;
    libprocess_set_status(threadsafe_stack_write(&init_objects.lockvspace,
                                                 &init_objects.vspace,
                                                 &handle->vspace,
                                                 &init_objects.vka,
                                                 handle->elf_phdrs,
                                                 handle->num_elf_phdrs * sizeof(Elf_Phdr),
                                                 &initial_stack_pointer));
    libprocess_guard(libprocess_get_status(), -6, libprocess_epilogue, 
                     "Failed to write the elf headers to the stack.");
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
    AUTOFREE uintptr_t *dest_argv = malloc(sizeof(uintptr_t) * argc);
    libprocess_check_malloc(dest_argv, libprocess_epilogue);
    AUTOFREE uintptr_t *dest_envp = malloc(sizeof(uintptr_t) * envc);
    libprocess_check_malloc(dest_envp, libprocess_epilogue);

    /* Copy the argv onto the stack. */
    libprocess_set_status(threadsafe_stack_copy_args(&init_objects.lockvspace,
                                                     &init_objects.vspace,
                                                     &handle->vspace,
                                                     &init_objects.vka,
                                                     argc,
                                                     argv,
                                                     dest_argv,
                                                     &initial_stack_pointer));
    libprocess_guard(libprocess_get_status(), -6, libprocess_epilogue, 
                     "Failed to copy argv onto the stack.");

    /* Copy the env onto the stack */
    libprocess_set_status(threadsafe_stack_copy_args(&init_objects.lockvspace,
                                       &init_objects.vspace,
                                       &handle->vspace,
                                       &init_objects.vka,
                                       envc,
                                       envp,
                                       dest_envp,
                                       &initial_stack_pointer));
    libprocess_guard(libprocess_get_status(), -6, libprocess_epilogue, 
                     "Failed to copy env onto the stack.");

    /**
     * We need to make sure the stack is aligned to a double word boundary
     * after we push on everything else below this point.
     * First, work out how much we are going to push
     */
    size_t to_push = 5 * sizeof(seL4_Word) +              /* constants */
                    sizeof(auxv[0]) * auxc +              /* aux */
                    sizeof(dest_argv[0]) * argc +         /* args */
                    sizeof(dest_envp[0]) * envc;          /* env */
    uintptr_t hypothetical_stack_pointer = initial_stack_pointer - to_push;
    uintptr_t rounded_stack_pointer = ALIGN_DOWN(hypothetical_stack_pointer, STACK_CALL_ALIGNMENT);
    ptrdiff_t stack_rounding = hypothetical_stack_pointer - rounded_stack_pointer;
    initial_stack_pointer -= stack_rounding;


    /**
     * Construct initial stack frame.
     * Working from top to bottom.
     */

    /* Null terminate aux */
    libprocess_set_status(threadsafe_stack_write_constant(&init_objects.lockvspace,
                                                          &init_objects.vspace,
                                                          &handle->vspace,
                                                          &init_objects.vka,
                                                          0,
                                                          &initial_stack_pointer));
    libprocess_guard(libprocess_get_status(), -6, libprocess_epilogue, 
                     "Failed to write arugments to new process stack");

    libprocess_set_status(threadsafe_stack_write_constant(&init_objects.lockvspace,
                                                          &init_objects.vspace,
                                                          &handle->vspace,
                                                          &init_objects.vka,
                                                          0,
                                                          &initial_stack_pointer));
    libprocess_guard(libprocess_get_status(), -6, libprocess_epilogue, 
                     "Failed to write arugments to new process stack");

    /* write aux */
    libprocess_set_status(threadsafe_stack_write(&init_objects.lockvspace,
                                                  &init_objects.vspace,
                                                  &handle->vspace,
                                                  &init_objects.vka,
                                                  auxv,
                                                  sizeof(auxv[0]) * auxc,
                                                  &initial_stack_pointer));
    libprocess_guard(libprocess_get_status(), -6, libprocess_epilogue, 
                     "Failed to write arugments to new process stack");

    /* Null terminate environment */
    libprocess_set_status(threadsafe_stack_write_constant(&init_objects.lockvspace,
                                                          &init_objects.vspace,
                                                          &handle->vspace,
                                                          &init_objects.vka,
                                                          0,
                                                          &initial_stack_pointer));
    libprocess_guard(libprocess_get_status(), -6, libprocess_epilogue, 
                     "Failed to write arugments to new process stack");

    /* write environment */
    libprocess_set_status(threadsafe_stack_write(&init_objects.lockvspace,
                                                 &init_objects.vspace,
                                                 &handle->vspace,
                                                 &init_objects.vka,
                                                 dest_envp,
                                                 sizeof(dest_envp[0]) * envc,
                                                 &initial_stack_pointer));
    libprocess_guard(libprocess_get_status(), -6, libprocess_epilogue, 
                     "Failed to write arugments to new process stack");

    /* Null terminate arguments */
    libprocess_set_status(threadsafe_stack_write_constant(&init_objects.lockvspace,
                                                          &init_objects.vspace,
                                                          &handle->vspace,
                                                          &init_objects.vka,
                                                          0,
                                                          &initial_stack_pointer));
    libprocess_guard(libprocess_get_status(), -6, libprocess_epilogue, 
                     "Failed to write arugments to new process stack");

    /* write arguments */
    libprocess_set_status(threadsafe_stack_write(&init_objects.lockvspace,
                                                 &init_objects.vspace,
                                                 &handle->vspace,
                                                 &init_objects.vka,
                                                 dest_argv,
                                                 sizeof(dest_argv[0]) * argc,
                                                 &initial_stack_pointer));
    libprocess_guard(libprocess_get_status(), -6, libprocess_epilogue, 
                     "Failed to write arugments to new process stack");

    /* Push argument count */
    libprocess_set_status(threadsafe_stack_write_constant(&init_objects.lockvspace,
                                                          &init_objects.vspace,
                                                          &handle->vspace,
                                                          &init_objects.vka,
                                                          argc,
                                                          &initial_stack_pointer));
    libprocess_guard(libprocess_get_status(), -6, libprocess_epilogue, 
                     "Failed to write arugments to new process stack");

    assert(initial_stack_pointer % (2 * sizeof(seL4_Word)) == 0);

    /**
     * Setup the initial register values for our process
     */
    libprocess_set_status(sel4utils_arch_init_context(handle->entry_point, 
                                                      (void *) initial_stack_pointer,
                                                      &context));
    libprocess_guard(libprocess_get_status(), -6, libprocess_epilogue, 
                     "Failed to initialize process context");

    /**
     * Start the thread running.
     */
    libprocess_set_status(seL4_TCB_WriteRegisters(handle->main_thread->tcb.cptr,
                                    1, /* Resume */
                                    0, /* Arch flags */
                                    sizeof(context)/sizeof(seL4_Word),
                                    &context));
    libprocess_guard(libprocess_get_status(), -6, libprocess_epilogue, 
                     "Failed to write registers for new process");

   libprocess_return_success();
   libprocess_epilogue();
}





