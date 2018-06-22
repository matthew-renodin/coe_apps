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
    handle->running = 0;


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
    if(handle->attrs.heap_size_pages > 0) {
        reservation_t res = vspace_reserve_range_at(&handle->vspace,
                                                    INIT_CHILD_HEAP_ADDR,
                                                    handle->attrs.heap_size_pages * PAGE_SIZE_4K,
                                                    seL4_AllRights, /* TODO Prune heap perms? */
                                                    1);
        if(res.res == 0) {
            ZF_LOGE("Failed to reserve space for the heap.");
            return -5;
        }
    
        error = vspace_new_pages_at_vaddr(&handle->vspace,
                                          INIT_CHILD_HEAP_ADDR,
                                          handle->attrs.heap_size_pages,
                                          PAGE_BITS_4K,
                                          res);
        if(error) {
            ZF_LOGE("Failed to map in the heap.");
            return error;
        }
    }

    handle->cnode_root_data = api_make_guard_skip_word(seL4_WordBits - handle->attrs.cnode_size_bits);

    /**
     * Setup the first thread in our new process
     */
    error = thread_handle_create_custom(handle->cnode.cptr,
                                        handle->cnode_root_data,
                                        handle->fault_ep.cptr,
                                        handle->page_dir.cptr,
                                        &handle->vspace,
                                        handle->attrs.stack_size_pages,
                                        handle->attrs.priority,
                                        handle->attrs.cpu_affinity,
                                        &handle->main_thread);
    if(error) {
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
    vka_cspace_make_path(&init_objects.vka, handle->main_thread.tcb.cptr, &src);
    error = vka_cnode_copy(&dst, &src, seL4_AllRights);
    if(error) {
        ZF_LOGE("Failed to copy cap into child cnode.");
        return error;
    }

    handle->cnode_next_free = INIT_CHILD_FIRST_FREE_SLOT;
    init_data__init(&handle->init_data);

#ifdef CONFIG_DEBUG_BUILD
    seL4_DebugNameThread(handle->main_thread.tcb.cptr, proc_name);
#endif
    handle->name = proc_name;
    handle->init_data.proc_name = proc_name;
    handle->ep_list_tail = NULL;

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

    if(handle == NULL || handle->entry_point == NULL || handle->main_thread.stack_vaddr == NULL) {
        ZF_LOGE("Invalid process handle pointer passed to process_run.");
        return -2; /* TODO come up with error codes */
    }

    if(handle->running) {
        ZF_LOGW("Process is already running.");
        return -3; /* TODO come up with error codes */
    }


    /**
     * Copy the init data into the child memory space
     */
    seL4_Word raw_size = init_data__get_packed_size(&handle->init_data);
    seL4_Word init_data_len = ROUND_UP(raw_size + sizeof(seL4_Word), PAGE_SIZE_4K);
    ZF_LOGV("Starting process with init data size: %lu", raw_size);

    reservation_t init_data_res = vspace_reserve_range_at(&handle->vspace,
                                                          INIT_CHILD_INIT_DATA_ADDR,
                                                          init_data_len,
                                                          seL4_AllRights, /* TODO: this ok? */
                                                          1);
    if(init_data_res.res == 0) {
        ZF_LOGE("Failed to reserve space for the init data");
        return -4;
    }

    error = vspace_new_pages_at_vaddr(&handle->vspace,
                                      INIT_CHILD_INIT_DATA_ADDR,
                                      init_data_len / PAGE_SIZE_4K,
                                      PAGE_BITS_4K,
                                      init_data_res);
    if(error) {
        ZF_LOGE("Failed to allocate space for the init data");
        return -5;
    }

    
    void * packed_init_data = vspace_share_mem(&handle->vspace,
                                               &init_objects.vspace,
                                               INIT_CHILD_INIT_DATA_ADDR,
                                               init_data_len / PAGE_SIZE_4K,
                                               PAGE_BITS_4K,
                                               seL4_AllRights,
                                               1); /* should this be cacheable? */
    if(packed_init_data == NULL) {
        ZF_LOGE("Failed to share init_data.");
        return -6;
    }

    /**
     * We first write then length
     */
    *((seL4_Word*)packed_init_data) = raw_size;

    /**
     * Then write the packed data
     */
    init_data__pack(&handle->init_data, packed_init_data + sizeof(seL4_Word));

    /**
     * We don't need the data in our address space anymore, unmap
     */
    sel4utils_unmap_pages(&init_objects.vspace,
                          packed_init_data,
                          init_data_len / PAGE_SIZE_4K,
                          PAGE_BITS_4K,
                          &init_objects.vka);

    
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
     *  TODO: figure out what this strings are used for if they are used at all.
     */
    int envc = 1;
    AUTOFREE char *ipc_buf_env = NULL;
    error = asprintf(&ipc_buf_env, "IPCBUFFER=0x%"PRIxPTR"", handle->main_thread.ipc_buffer_vaddr);
    if (error == -1) {
        return -1;
    }
    AUTOFREE char *tcb_cptr_buf_env = NULL;
    error = asprintf(&tcb_cptr_buf_env, "boot_tcb_cptr=0x%"PRIxPTR"", handle->main_thread.tcb.cptr);
    if (error == -1) {
        return -1;
    }
    char *envp[] = {ipc_buf_env, tcb_cptr_buf_env};


    uintptr_t initial_stack_pointer = (uintptr_t)handle->main_thread.stack_vaddr - sizeof(seL4_Word); 
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
    error = seL4_TCB_WriteRegisters(handle->main_thread.tcb.cptr,
                                    1, /* Resume */
                                    0, /* Arch flags */
                                    sizeof(context)/sizeof(seL4_Word),
                                    &context);
    if(error) {
        ZF_LOGE("Failed to write registers for new process");
        return error;
    }

   return 0; 
}


/**
 * Assumes that handle is valid and that you have all the locks necessarry
 */
static seL4_CPtr copy_cap_into_next_slot(process_handle_t *handle, seL4_CPtr new_cap)
{
    int error;

    cspacepath_t dst, src;
    dst.root = handle->cnode.cptr;
    dst.capDepth = handle->attrs.cnode_size_bits;
    dst.capPtr = handle->cnode_next_free;

    vka_cspace_make_path(&init_objects.vka, new_cap, &src);
    error = vka_cnode_copy(&dst, &src, seL4_AllRights);
    if(error) {
        ZF_LOGE("Failed to copy cap into child cnode.");
        return seL4_CapNull;
    }

    return handle->cnode_next_free++;
    
}


int process_add_device_pages(process_handle_t *handle, 
                             void *paddr,
                             seL4_Word num_pages,
                             const char* device_name)
{
    /* TODO: Implement */
    return 0;
}


int process_add_device_irq(process_handle_t *handle,
                           int irq_number,
                           const char* device_name)
{
    /* TODO: Implement */
    return 0;
}


/**
 * This helper assumes you have grabbed all the locks
 */
static int copy_ep_to_proc(process_handle_t *handle, seL4_CPtr ep_cap, const char *conn_name) 
{
    /* TODO no pastarino? */
    EndpointData *ep_data = malloc(sizeof(EndpointData));
    if(ep_data == NULL) {
        ZF_LOGE("Failed to allocate Endpoint Data");
        return -1;
    }

    endpoint_data__init(ep_data);
    ep_data->name = conn_name;
    ep_data->cap = copy_cap_into_next_slot(handle, ep_cap);
    if(ep_data->cap == seL4_CapNull) {
        ZF_LOGE("Failed to copy ep cap");
        return -2;
    }

    if(handle->ep_list_tail == NULL) {
        /* We are adding the first ep */
        handle->init_data.ep_list_head = ep_data;
        handle->ep_list_tail = ep_data;
    } else {
        /* Else add to tail */
        handle->ep_list_tail->next = ep_data;
        handle->ep_list_tail = ep_data;
    }

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

    vka_object_t ep;
    error = vka_alloc_endpoint(&init_objects.vka, &ep);
    if(error) {
        ZF_LOGE("Failed to allocate ep object.");
        return -3;
    }

    error = copy_ep_to_proc(handle1, ep.cptr, conn_name);
    if(error) {
        ZF_LOGE("Failed to copy ep to process 1");
        return -4;
    }
    
    error = copy_ep_to_proc(handle2, ep.cptr, strdup(conn_name));
    if(error) {
        ZF_LOGE("Failed to copy ep to process 2");
        return -5;
    }

    return 0;
}


int process_connect_shmem(process_handle_t *handle1, seL4_CapRights_t perms1, 
                          process_handle_t *handle2, seL4_CapRights_t perms2,
                          seL4_Word num_pages,
                          const char *conn_name)
{
    /* TODO: Implement */
    return 0;
}


int process_connect_notification(process_handle_t *handle1, seL4_CapRights_t perms1,
                                 process_handle_t *handle2, seL4_CapRights_t perms2,
                                 const char *conn_name)
{
    /* TODO: Implement */
    return 0;
}



int process_give_untyped_resources(process_handle_t *handle,
                                   seL4_Word length_bytes,
                                   seL4_Word num_objects)
{
    /* TODO: Implement */
    return 0;
}



