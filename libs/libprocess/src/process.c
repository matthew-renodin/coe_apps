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
 * @file process.c
 * @brief Core libprocess implementation.
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



const process_attr_t process_default_attrs = {
    .heap_size_pages    = CONFIG_LIB_PROCESS_DEFAULT_HEAP_SIZE_PAGES,
    .stack_size_pages   = CONFIG_LIB_PROCESS_DEFAULT_STACK_SIZE_PAGES,
    .priority           = CONFIG_LIB_PROCESS_DEFAULT_PRIORITY,
    .cpu_affinity       = CONFIG_LIB_PROCESS_DEFAULT_CPU_AFFINITY,
    .cnode_size_bits    = CONFIG_LIB_PROCESS_DEFAULT_CNODE_SIZE_BITS,
};


int process_create(const char *elf_file_name,
                   const char *proc_name,
                   const process_attr_t *attr,
                   process_handle_t *handle)
{
    int error;

    /* TODO: Implement sync for init objects */
    if(!init_objects.initialized) {
        ZF_LOGW("Init objects (vka, vspace) have not been setup.\n"
                "Run init_process or init_root_task to complete.");
        return -1;
    }

    if(handle == NULL) {
        ZF_LOGE("Null process handle pointer passed to process_create.");
        return -2; /* TODO come up with error codes */
    }
    memset((void *)handle, 0, sizeof(handle));


    /* Keep our own copy of the attrs for future reference, if it's null use the defaults */
    handle->attrs = (attr == NULL) ? process_default_attrs : *attr;
 

    /**
     * Create all the objects that are shared among the threads in a process
     */
    error = vka_alloc_cnode_object(&init_objects.vka,
                                   handle->attrs.cnode_size_bits,
                                   &handle->cnode);
    if(error) {
        ZF_LOGE("Failed to allocate a cnode.");
        return error;
    }
    
    error = vka_alloc_endpoint(&init_objects.vka, &handle->fault_ep);
    if(error) {
        ZF_LOGE("Failed to allocate a fault endpoint.");
        return error;
    }

    error = vka_alloc_vspace_root(&init_objects.vka, &handle->page_dir);
    if(error) {
        ZF_LOGE("Failed to allocate a page dir.");
        return error;
    }


#ifndef CONFIG_ARCH_X86_64
    /**
     * Assign the new vspace to our current asid_pool. If a process doesn't 
     * have a pool cap, then it cannot create address spaces
     */
    error = seL4_ARCH_ASIDPool_Assign(init_objects.asid_pool_cap, handle->page_dir.cptr);
    if(error) {
        ZF_LOGE("Failed to assign an ASID.");
        return error;
    }
#endif

    /**
     * Setup the new process's virtual memory bookkeeping object
     */
    error = sel4utils_get_vspace(&init_objects.vspace,
                                 &handle->vspace,
                                 &handle->vspace_data,
                                 &init_objects.vka,
                                 handle->page_dir.cptr,
                                 NULL,  /* Optional function to call when objects are allocated */
                                 NULL); /* Optional args. */
    if(error) {
        ZF_LOGE("Failed to create child process vspace object");
        return error;
    }

    /**
     * Load the elf file into the new address space
     */ 
    handle->entry_point = sel4utils_elf_load(&handle->vspace,
                                             &init_objects.vspace,
                                             &init_objects.vka,
                                             &init_objects.vka,
                                             elf_file_name);
    if(handle->entry_point == NULL) { 
        ZF_LOGE("Failed to load elf file.");
        return -3;
    }

    /**
     * Record some metadata from the elf file. This is necesarry for setting up 
     * libc in the child process.
     */
    handle->sysinfo = sel4utils_elf_get_vsyscall(elf_file_name);
    handle->num_elf_phdrs = sel4utils_elf_num_phdrs(elf_file_name);
    handle->elf_phdrs = calloc(handle->num_elf_phdrs, sizeof(Elf_Phdr));
    if(handle->elf_phdrs == NULL) {
        ZF_LOGE("Failed to allocate memory for the elf phdrs.");
        return -4;
    }
    sel4utils_elf_read_phdrs(elf_file_name, handle->num_elf_phdrs, handle->elf_phdrs);


    /**
     * Allocate a heap and map it into the process's page directory.
     */
    error = mmap_new_pages_custom(&handle->vspace,
                                  handle->page_dir.cptr,
                                  handle->attrs.heap_size_pages,
                                  &mmap_attr_4k_data,
                                  NULL,
                                  &handle->heap_vaddr);
    if(error) {
        ZF_LOGE("Failed to map in the heap.");
        return error;
    }

    handle->cnode_root_data = api_make_guard_skip_word(seL4_WordBits - handle->attrs.cnode_size_bits);

    /**
     * Setup the first thread in our new process
     */
    thread_attr_t thread_attr = {
        .stack_size_pages = handle->attrs.stack_size_pages,
        .priority = handle->attrs.priority,
        .cpu_affinity = handle->attrs.cpu_affinity,
    };
    handle->main_thread = thread_handle_create_custom(handle->cnode.cptr,
                                                      handle->cnode_root_data,
                                                      handle->fault_ep.cptr,
                                                      handle->page_dir.cptr,
                                                      &handle->vspace,
                                                      &thread_attr);
    if(handle->main_thread == NULL) {
        ZF_LOGE("Failed to create a thread.");
        return error;
    }

    /**
     * Copy caps to new cnode
     */
    cspacepath_t dst, src;
    dst.root = handle->cnode.cptr;
    dst.capDepth = handle->attrs.cnode_size_bits;
    
    dst.capPtr = INIT_CHILD_CNODE_SLOT;
    vka_cspace_make_path(&init_objects.vka, handle->cnode.cptr, &src);
    error = vka_cnode_mint(&dst,
                           &src,
                           seL4_AllRights,
                           handle->cnode_root_data);
    if(error) {
        ZF_LOGE("Failed to copy cap into child cnode.");
        return error;
    }
    
    dst.capPtr = INIT_CHILD_FAULT_EP_SLOT;
    vka_cspace_make_path(&init_objects.vka, handle->fault_ep.cptr, &src);
    error = vka_cnode_copy(&dst, &src, seL4_AllRights);
    if(error) {
        ZF_LOGE("Failed to copy cap into child cnode.");
        return error;
    }

    dst.capPtr = INIT_CHILD_PAGE_DIR_SLOT;
    vka_cspace_make_path(&init_objects.vka, handle->page_dir.cptr, &src);
    error = vka_cnode_copy(&dst, &src, seL4_AllRights);
    if(error) {
        ZF_LOGE("Failed to copy cap into child cnode.");
        return error;
    }

    dst.capPtr = INIT_CHILD_TCB_SLOT;
    vka_cspace_make_path(&init_objects.vka, handle->main_thread->tcb.cptr, &src);
    error = vka_cnode_copy(&dst, &src, seL4_AllRights);
    if(error) {
        ZF_LOGE("Failed to copy cap into child cnode.");
        return error;
    }

    dst.capPtr = INIT_CHILD_SYNC_NOTIFICATION_SLOT;
    vka_cspace_make_path(&init_objects.vka, handle->main_thread->sync_notification.cptr, &src);
    error = vka_cnode_copy(&dst, &src, seL4_AllRights);
    if(error) {
        ZF_LOGE("Failed to copy cap into child cnode.");
        return error;
    }

    handle->cnode_next_free = INIT_CHILD_FIRST_FREE_SLOT;
    init_data__init(&handle->init_data);

#ifdef CONFIG_DEBUG_BUILD
    seL4_DebugNameThread(handle->main_thread->tcb.cptr, proc_name);
#endif
    handle->running = 0;
    handle->name = proc_name;
    handle->init_data.proc_name = (char *)proc_name; /* protobuf uses non const strings */
    handle->init_data.cnode_size_bits = handle->attrs.cnode_size_bits;
    handle->init_data.stack_size_pages = handle->attrs.stack_size_pages; 
    handle->init_data.stack_vaddr = (seL4_Word)handle->main_thread->stack_vaddr;

    return 0;
}



int process_run(process_handle_t *handle, int argc, char *argv[])
{
    int error;

    /* TODO: Implement sync for init objects */
    if(!init_objects.initialized) {
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
    error = mmap_new_pages_custom(&handle->vspace,
                                  handle->page_dir.cptr,
                                  init_data_len / PAGE_SIZE_4K,
                                  &mmap_attr_4k_data,
                                  NULL,
                                  &init_data_vaddr);
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
     * We need to copy each cap into the child's cnode (overwriting caps[])
     */
    if(add_caps) {
        for(i = 0; i < num_pages; i++) {
            caps[i] = copy_cap_into_next_slot(handle, caps[i], seL4_AllRights); 
        }
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

    return 0;
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
        return -5;
    }
    
    error = copy_ep_to_proc(handle2, ep.cptr, perms2, conn_name);
    if(error) {
        ZF_LOGE("Failed to copy ep to process 2");
        return -6;
    }

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

int process_connect_shmem(process_handle_t *handle1, seL4_CapRights_t perms1, 
                          process_handle_t *handle2, seL4_CapRights_t perms2,
                          seL4_Word num_pages,
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
        return -5;
    }
    
    error = copy_notification_to_proc(handle2, ep.cptr, perms2, conn_name);
    if(error) {
        ZF_LOGE("Failed to copy notification to process 2");
        return -6;
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
    int error;

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

    free(caps);

    error = copy_shmem_to_proc(handle, vaddr_child, num_pages, conn_name);
    if(error) {
        ZF_LOGE("Failed to copy shemem data to proc");
        return -8;
    }

    return 0;
}


