/**
 * @file create.c
 * @brief Implementation of process_create.
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
#include <lockwrapper/lockwrapper.h>

int process_lib_lock_initialized = 0;
mutex_t process_lib_lock = {0};

/**
 * When vspace allocates a page table, it calls this function to tell us about it.
 */
void process_allocated_object(void* cookie, vka_object_t obj)
{
    process_handle_t *handle = (process_handle_t*)cookie;
    process_object_t *new_object = (process_object_t*)malloc(sizeof(process_object_t));
    new_object->obj = obj;
    new_object->next = handle->vspace_allocation_list;
    handle->vspace_allocation_list = new_object; 
}

int process_create(const char *elf_file_name,
                   const char *proc_name,
                   const process_attr_t *attr,
                   process_handle_t *handle)
{
    int error;

    libprocess_prologue();

    libprocess_check_initialized();

    libprocess_check_arg(handle);
    libprocess_check_arg(proc_name);
    libprocess_check_arg(elf_file_name);

    memset((void *)handle, 0, sizeof(handle));

    /* Keep our own copy of the attrs for future reference, if it's null use the defaults */
    handle->attrs = (attr == NULL) ? process_default_attrs : *attr;
 
    handle->name = proc_name;
    handle->state = PROCESS_INIT;
    handle->vspace_allocation_list = NULL;
    handle->untyped_allocation_list = NULL;
    handle->shared_objects = NULL;

    /**
     * Create all the objects that are shared among the threads in a process
     */
    error = vka_alloc_cnode_object(&init_objects.vka,
                                   handle->attrs.cnode_size_bits,
                                   &handle->cnode);
    libprocess_guard(error, -5, libprocess_epilogue, "Failed to allocate a cnode.");

    if(handle->attrs.create_fault_ep) {
        error = vka_alloc_endpoint(&init_objects.vka, &handle->fault_ep);
        libprocess_guard(error, -5, alloc_fep_fail, "Failed to allocate a fault endpoint.");
    } else {
        /* TODO just setting this field feels dirty */
        handle->fault_ep.cptr = handle->attrs.existing_fault_ep;
    }

    error = vka_alloc_vspace_root(&init_objects.vka, &handle->page_dir);
    libprocess_guard(error, -5, alloc_vspace_fail, "Failed to allocate a page dir.");
    
    error = vka_alloc_notification(&init_objects.vka, &handle->vspace_lock_notification);
    libprocess_guard(error, -5, alloc_vspace_lock_fail, "Failed to allocate a notification.");

    error = vka_alloc_notification(&init_objects.vka, &handle->vka_lock_notification);
    libprocess_guard(error, -5, alloc_vka_lock_fail, "Failed to allocate a notification.");

    error = vka_alloc_notification(&init_objects.vka, &handle->init_data_lock_notification);
    libprocess_guard(error, -5, alloc_init_lock_fail, "Failed to allocate a notification.");


#ifndef CONFIG_ARCH_X86_64
    /**
     * Assign the new vspace to our current asid_pool. If a process doesn't 
     * have a pool cap, then it cannot create address spaces
     */
    error = seL4_ARCH_ASIDPool_Assign(init_objects.asid_pool_cap, handle->page_dir.cptr);
    libprocess_guard(error, -6, alloc_init_lock_fail, "Failed to assign an ASID.");
#endif


    /**
     * Setup the new process's virtual memory bookkeeping object
     */
    lockvspace_lock(&init_objects.vspace, &init_objects.lockvspace);
    error = sel4utils_get_vspace(&init_objects.lockvspace.parent_vspace,
                                 &handle->vspace,
                                 &handle->vspace_data,
                                 &init_objects.vka,
                                 handle->page_dir.cptr,
                                 process_allocated_object,  /* Optional function to call when objects are allocated */
                                 (void*)handle); /* Optional args. */
    lockvspace_unlock(&init_objects.vspace, &init_objects.lockvspace);
    libprocess_guard(error, -7, get_vspace_fail, "Failed to create child process vspace object");

    /**
     * Load the elf file into the new address space
     */ 
    lockvspace_lock(&init_objects.vspace, &init_objects.lockvspace);
    handle->entry_point = sel4utils_elf_load(&handle->vspace,
                                             &init_objects.lockvspace.parent_vspace,
                                             &init_objects.vka,
                                             &init_objects.vka,
                                             elf_file_name);
    lockvspace_unlock(&init_objects.vspace, &init_objects.lockvspace);
    libprocess_guard(handle->entry_point == NULL, -8, elf_load_fail, "Failed to load elf file.");

    /**
     * Record some metadata from the elf file. This is necesarry for setting up 
     * libc in the child process.
     */
    handle->sysinfo = sel4utils_elf_get_vsyscall(elf_file_name);
    handle->num_elf_phdrs = sel4utils_elf_num_phdrs(elf_file_name);
    handle->elf_phdrs = calloc(handle->num_elf_phdrs, sizeof(Elf_Phdr));
    libprocess_guard(handle->elf_phdrs == NULL, -9, elf_phdrs_fail,
                     "Failed to allocate memory for the elf phdrs.");
    sel4utils_elf_read_phdrs(elf_file_name, handle->num_elf_phdrs, handle->elf_phdrs);

    /**
     * Allocate a heap and map it into the process's page directory.
     */
    error = mmap_new_pages_custom(&handle->vspace,
                                  handle->page_dir.cptr,
                                  handle->attrs.heap_size_pages,
                                  &mmap_attr_4k_data,
                                  NULL,
                                  &handle->heap_vaddr,
                                  &handle->heap_res);
    libprocess_guard(error, -10, map_heap_fail, "Failed to map in the heap.");

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
    libprocess_guard(handle->main_thread == NULL, -11, thread_create_fail, "Failed to create a thread.");

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
    libprocess_guard(error, -12, copy_cap_fail, "Failed to copy cap into child cnode.");
  
    if(handle->fault_ep.cptr != seL4_CapNull) {
        dst.capPtr = INIT_CHILD_FAULT_EP_SLOT;
        vka_cspace_make_path(&init_objects.vka, handle->fault_ep.cptr, &src);
        error = vka_cnode_copy(&dst, &src, seL4_AllRights);
        libprocess_guard(error, -12, copy_cap_fail, "Failed to copy cap into child cnode.");
    }

    dst.capPtr = INIT_CHILD_PAGE_DIR_SLOT;
    vka_cspace_make_path(&init_objects.vka, handle->page_dir.cptr, &src);
    error = vka_cnode_copy(&dst, &src, seL4_AllRights);
    libprocess_guard(error, -12, copy_cap_fail, "Failed to copy cap into child cnode.");

    dst.capPtr = INIT_CHILD_TCB_SLOT;
    vka_cspace_make_path(&init_objects.vka, handle->main_thread->tcb.cptr, &src);
    error = vka_cnode_copy(&dst, &src, seL4_AllRights);
    libprocess_guard(error, -12, copy_cap_fail, "Failed to copy cap into child cnode.");

    dst.capPtr = INIT_CHILD_VSPACE_LOCK_SLOT;
    vka_cspace_make_path(&init_objects.vka, handle->vspace_lock_notification.cptr, &src);
    error = vka_cnode_copy(&dst, &src, seL4_AllRights);
    libprocess_guard(error, -12, copy_cap_fail, "Failed to copy cap into child cnode.");

    dst.capPtr = INIT_CHILD_VKA_LOCK_SLOT;
    vka_cspace_make_path(&init_objects.vka, handle->vka_lock_notification.cptr, &src);
    error = vka_cnode_copy(&dst, &src, seL4_AllRights);
    libprocess_guard(error, -12, copy_cap_fail, "Failed to copy cap into child cnode.");
    
    dst.capPtr = INIT_CHILD_INIT_OBJECTS_LOCK_SLOT;
    vka_cspace_make_path(&init_objects.vka, handle->init_data_lock_notification.cptr, &src);
    error = vka_cnode_copy(&dst, &src, seL4_AllRights);
    libprocess_guard(error, -12, copy_cap_fail, "Failed to copy cap into child cnode.");

    dst.capPtr = INIT_CHILD_SYNC_NOTIFICATION_SLOT;
    vka_cspace_make_path(&init_objects.vka, handle->main_thread->sync_notification.cptr, &src);
    error = vka_cnode_copy(&dst, &src, seL4_AllRights);
    libprocess_guard(error, -12, copy_cap_fail, "Failed to copy cap into child cnode.");


    handle->cnode_next_free = INIT_CHILD_FIRST_FREE_SLOT;
    init_data__init(&handle->init_data);

#ifdef CONFIG_DEBUG_BUILD
    seL4_DebugNameThread(handle->main_thread->tcb.cptr, handle->name);
#endif
    handle->init_data.proc_name = (char *)proc_name; /* protobuf uses non const strings */
    handle->init_data.cnode_size_bits = handle->attrs.cnode_size_bits;
    handle->init_data.stack_size_pages = handle->attrs.stack_size_pages; 
    handle->init_data.stack_vaddr = (seL4_Word)handle->main_thread->stack_vaddr;


    libprocess_return_success();

    copy_cap_fail:
        thread_destroy_free_handle_custom(&handle->main_thread, &handle->vspace);
    thread_create_fail:
    map_heap_fail:
        free(&handle->elf_phdrs);
    elf_phdrs_fail:
    elf_load_fail:
        vspace_tear_down(&handle->vspace, VSPACE_FREE);
    get_vspace_fail:
        vka_free_object(&init_objects.vka, &handle->init_data_lock_notification);
    alloc_init_lock_fail:
        vka_free_object(&init_objects.vka, &handle->vka_lock_notification);
    alloc_vka_lock_fail:
        vka_free_object(&init_objects.vka, &handle->vspace_lock_notification);
    alloc_vspace_lock_fail:
        vka_free_object(&init_objects.vka, &handle->page_dir);
    alloc_vspace_fail:
        if(handle->fault_ep.cptr != seL4_CapNull) {
            vka_free_object(&init_objects.vka, &handle->fault_ep);
        }
    alloc_fep_fail:
        vka_free_object(&init_objects.vka, &handle->cnode);
    
    libprocess_epilogue();
}


