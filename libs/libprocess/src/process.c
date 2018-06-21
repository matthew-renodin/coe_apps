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
    if(!init_objects.initialized) return -1;

    if(handle == NULL) return -2; /* TODO come up with error codes */
    handle->running = 0;


    /* Keep our own copy of the attrs for future reference, if it's null use the defaults */
    handle->attrs = (attr == NULL) ? process_default_attrs : *attr;
 

    /**
     * Create all the objects that are shared among the threads in a process
     */
    error = vka_alloc_cnode_object(&init_objects.vka,
                                   handle->attrs.cnode_size_bits,
                                   &handle->cnode);
    if(error) return error;
    
    error = vka_alloc_endpoint(&init_objects.vka, &handle->fault_ep);
    if(error) return error;

    error = vka_alloc_vspace_root(&init_objects.vka, &handle->page_dir);
    if(error) return error;


#ifndef CONFIG_ARCH_X86_64
    /**
     * Assign the new vspace to our current asid_pool. If a process doesn't 
     * have a pool cap, then it cannot create address spaces
     */
    error = seL4_ARCH_ASIDPool_Assign(init_objects.asid_pool_cap, handle->page_dir.cptr);
    if(error != seL4_NoError) return error;
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
    if(error) return error;    


    /**
     * Load the elf file into the new address space
     */ 
    handle->entry_point = sel4utils_elf_load(&handle->vspace,
                                             &init_objects.vspace,
                                             &init_objects.vka,
                                             &init_objects.vka,
                                             elf_file_name);
    if(handle->entry_point == NULL) return -4;

    handle->num_elf_phdrs = sel4utils_elf_num_phdrs(elf_file_name);
    handle->elf_phdrs = calloc(handle->num_elf_phdrs, sizeof(Elf_Phdr));
    handle->sysinfo = sel4utils_elf_get_vsyscall(elf_file_name);

    /**
     * Allocate a heap and map it into the process's page directory.
     */
    reservation_t res = vspace_reserve_range_at(&handle->vspace,
                                                INIT_CHILD_HEAP_ADDR,
                                                handle->attrs.heap_size_pages * PAGE_SIZE_4K,
                                                seL4_AllRights, /* TODO Prune heap perms? */
                                                1);
    if(res.res == 0) return -3;

    error = vspace_new_pages_at_vaddr(&handle->vspace,
                                      INIT_CHILD_HEAP_ADDR,
                                      handle->attrs.heap_size_pages,
                                      PAGE_BITS_4K,
                                      res);
    if(error) return error;


    /**
     * Setup the first thread in our new process
     */
    error = thread_handle_create_custom(handle->cnode.cptr,
                                        handle->fault_ep.cptr,
                                        handle->page_dir.cptr,
                                        &handle->vspace,
                                        handle->attrs.stack_size_pages,
                                        handle->attrs.priority,
                                        handle->attrs.cpu_affinity,
                                        &handle->main_thread);
    if(error) return error; 

    /**
     * Copy caps to new cnode
     */
    cspacepath_t dst, src;
    dst.root = handle->cnode.cptr;
    dst.capDepth = handle->attrs.cnode_size_bits;
    
    dst.capPtr = INIT_CHILD_CNODE_SLOT;
    vka_cspace_make_path(&init_objects.vka, handle->cnode.cptr, &src);
    error = vka_cnode_copy(&dst, &src, seL4_AllRights);
    if(error) return error;
    
    dst.capPtr = INIT_CHILD_FAULT_EP_SLOT;
    vka_cspace_make_path(&init_objects.vka, handle->fault_ep.cptr, &src);
    error = vka_cnode_copy(&dst, &src, seL4_AllRights);
    if(error) return error;

    dst.capPtr = INIT_CHILD_PAGE_DIR_SLOT;
    vka_cspace_make_path(&init_objects.vka, handle->page_dir.cptr, &src);
    error = vka_cnode_copy(&dst, &src, seL4_AllRights);
    if(error) return error;

    dst.capPtr = INIT_CHILD_TCB_SLOT;
    vka_cspace_make_path(&init_objects.vka, handle->main_thread.tcb.cptr, &src);
    error = vka_cnode_copy(&dst, &src, seL4_AllRights);
    if(error) return error;


    handle->name = proc_name;
#ifdef CONFIG_DEBUG_BUILD
    seL4_DebugNameThread(handle->main_thread.tcb.cptr, proc_name);
#endif

    handle->cnode_next_free = INIT_CHILD_FIRST_FREE_SLOT;

    return 0;
}


int process_run(process_handle_t *handle, int argc, char *argv[])
{
    int error;

    /* TODO: Implement sync for init objects */
    if(!init_objects.initialized) return -1;

    if(handle == NULL || handle->running) return -2; /* TODO come up with error codes */
    
    if(handle->entry_point == NULL || handle->main_thread.stack_vaddr == NULL) return -3;


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
    /* Copy the elf headers */
    uintptr_t at_phdr;
    error = sel4utils_stack_write(&init_objects.vspace, &handle->vspace, &init_objects.vka, handle->elf_phdrs,
                                  handle->num_elf_phdrs * sizeof(Elf_Phdr), &initial_stack_pointer);

    if (error) {
        return -1;
    }
    at_phdr = initial_stack_pointer;

    /* initialize of aux vectors */
    int auxc = 4;
    Elf_auxv_t auxv[5];
    auxv[0].a_type = AT_PAGESZ;
    auxv[0].a_un.a_val = PAGE_SIZE_4K;
    auxv[1].a_type = AT_PHDR;
    auxv[1].a_un.a_val = at_phdr;
    auxv[2].a_type = AT_PHNUM;
    auxv[2].a_un.a_val = handle->num_elf_phdrs;
    auxv[3].a_type = AT_PHENT;
    auxv[3].a_un.a_val = sizeof(Elf_Phdr);
    if(handle->sysinfo) {
        auxv[4].a_type = AT_SYSINFO;
        auxv[4].a_un.a_val = handle->sysinfo;
        auxc++;
    }

    seL4_UserContext context = {0};

    uintptr_t dest_argv[argc];
    uintptr_t dest_envp[envc];

    /* write all the strings into the stack */
    /* Copy over the user arguments */
    error = sel4utils_stack_copy_args(&init_objects.vspace, &handle->vspace, &init_objects.vka, argc, argv, dest_argv, &initial_stack_pointer);
    if (error) {
        return -1;
    }

    /* copy the environment */
    error = sel4utils_stack_copy_args(&init_objects.vspace, &handle->vspace, &init_objects.vka, envc, envp, dest_envp, &initial_stack_pointer);
    if (error) {
        return -1;
    }

    /* we need to make sure the stack is aligned to a double word boundary after we push on everything else
     * below this point. First, work out how much we are going to push */
    size_t to_push = 5 * sizeof(seL4_Word) + /* constants */
                    sizeof(auxv[0]) * auxc + /* aux */
                    sizeof(dest_argv) + /* args */
                    sizeof(dest_envp); /* env */
    uintptr_t hypothetical_stack_pointer = initial_stack_pointer - to_push;
    uintptr_t rounded_stack_pointer = ALIGN_DOWN(hypothetical_stack_pointer, STACK_CALL_ALIGNMENT);
    ptrdiff_t stack_rounding = hypothetical_stack_pointer - rounded_stack_pointer;
    initial_stack_pointer -= stack_rounding;

    /* construct initial stack frame */
    /* Null terminate aux */
    error = sel4utils_stack_write_constant(&init_objects.vspace, &handle->vspace, &init_objects.vka, 0, &initial_stack_pointer);
    if (error) {
        return -1;
    }
    error = sel4utils_stack_write_constant(&init_objects.vspace, &handle->vspace, &init_objects.vka, 0, &initial_stack_pointer);
    if (error) {
        return -1;
    }
    /* write aux */
    error = sel4utils_stack_write(&init_objects.vspace, &handle->vspace, &init_objects.vka, auxv, sizeof(auxv[0]) * auxc, &initial_stack_pointer);
    if (error) {
        return -1;
    }
    /* Null terminate environment */
    error = sel4utils_stack_write_constant(&init_objects.vspace, &handle->vspace, &init_objects.vka, 0, &initial_stack_pointer);
    if (error) {
        return -1;
    }
    /* write environment */
    error = sel4utils_stack_write(&init_objects.vspace, &handle->vspace, &init_objects.vka, dest_envp, sizeof(dest_envp), &initial_stack_pointer);
    if (error) {
        return -1;
    }
    /* Null terminate arguments */
    error = sel4utils_stack_write_constant(&init_objects.vspace, &handle->vspace, &init_objects.vka, 0, &initial_stack_pointer);
    if (error) {
        return -1;
    }
    /* write arguments */
    error = sel4utils_stack_write(&init_objects.vspace, &handle->vspace, &init_objects.vka, dest_argv, sizeof(dest_argv), &initial_stack_pointer);
    if (error) {
        return -1;
    }
    /* Push argument count */
    error = sel4utils_stack_write_constant(&init_objects.vspace, &handle->vspace, &init_objects.vka, argc, &initial_stack_pointer);
    if (error) {
        return -1;
    }

    assert(initial_stack_pointer % (2 * sizeof(seL4_Word)) == 0);
    error = sel4utils_arch_init_context(handle->entry_point, (void *) initial_stack_pointer, &context);
    if(error) {
        ZF_LOGW("Failed to initialize process context");
        return -3;
    }

    //process->thread.initial_stack_pointer = (void *) initial_stack_pointer;


    error = seL4_TCB_WriteRegisters(handle->main_thread.tcb.cptr,
                                    1, /* Resume */
                                    0, /* Arch flags */
                                    sizeof(context)/sizeof(seL4_Word),
                                    &context);
    if(error) {
        ZF_LOGW("Failed to write registers for new process");
        return -4;
    }

   printf("Started %s with sp:%p, entry:%p\n", handle->name, initial_stack_pointer, handle->entry_point);

   return 0; 
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


int process_connect_ep(process_handle_t *handle1, seL4_CapRights_t perms1,
                       process_handle_t *handle2, seL4_CapRights_t perms2,
                       const char *conn_name)
{
    /* TODO: Implement */
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



