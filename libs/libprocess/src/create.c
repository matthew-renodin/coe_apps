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

    /* TODO: Implement sync for init objects */
    if(!init_check_initialized()) {
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
    if(error) {
        ZF_LOGE("Failed to allocate a cnode.");
        return error;
    }
   
    if(handle->attrs.create_fault_ep) {
        error = vka_alloc_endpoint(&init_objects.vka, &handle->fault_ep);
        if(error) {
            ZF_LOGE("Failed to allocate a fault endpoint.");
            return error;
        }
    } else {
        /* TODO just setting this field feels dirty */
        handle->fault_ep.cptr = handle->attrs.existing_fault_ep;
    }

    error = vka_alloc_vspace_root(&init_objects.vka, &handle->page_dir);
    if(error) {
        ZF_LOGE("Failed to allocate a page dir.");
        return error;
    }

    error = vka_alloc_notification(&init_objects.vka, &handle->vspace_lock_notification);
    if(error) {
        ZF_LOGE("Failed to allocate a notification.");
        return error;
    }

    error = vka_alloc_notification(&init_objects.vka, &handle->vka_lock_notification);
    if(error) {
        ZF_LOGE("Failed to allocate a notification.");
        return error;
    }

    error = vka_alloc_notification(&init_objects.vka, &handle->init_data_lock_notification);
    if(error) {
        ZF_LOGE("Failed to allocate a notification.");
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
    lockvspace_lock(&init_objects.vspace, &init_objects.lockvspace);
    error = sel4utils_get_vspace(&init_objects.lockvspace.parent_vspace,
                                 &handle->vspace,
                                 &handle->vspace_data,
                                 &init_objects.vka,
                                 handle->page_dir.cptr,
                                 process_allocated_object,  /* Optional function to call when objects are allocated */
                                 (void*)handle); /* Optional args. */
    lockvspace_unlock(&init_objects.vspace, &init_objects.lockvspace);
    if(error) {
        ZF_LOGE("Failed to create child process vspace object");
        return error;
    }

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
                                  &handle->heap_vaddr,
                                  &handle->heap_res);
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
    ZF_LOGI("Address of src %p", src);
    
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
  
    if(handle->fault_ep.cptr != seL4_CapNull) {
        dst.capPtr = INIT_CHILD_FAULT_EP_SLOT;
        vka_cspace_make_path(&init_objects.vka, handle->fault_ep.cptr, &src);
        error = vka_cnode_copy(&dst, &src, seL4_AllRights);
        if(error) {
            ZF_LOGE("Failed to copy cap into child cnode.");
            return error;
        }
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

    dst.capPtr = INIT_CHILD_VSPACE_LOCK_SLOT;
    vka_cspace_make_path(&init_objects.vka, handle->vspace_lock_notification.cptr, &src);
    error = vka_cnode_copy(&dst, &src, seL4_AllRights);
    if(error) {
        ZF_LOGE("Failed to copy cap into child cnode.");
        return error;
    }

    dst.capPtr = INIT_CHILD_VKA_LOCK_SLOT;
    vka_cspace_make_path(&init_objects.vka, handle->vka_lock_notification.cptr, &src);
    error = vka_cnode_copy(&dst, &src, seL4_AllRights);
    if(error) {
        ZF_LOGE("Failed to copy cap into child cnode.");
        return error;
    }
    
    dst.capPtr = INIT_CHILD_INIT_OBJECTS_LOCK_SLOT;
    vka_cspace_make_path(&init_objects.vka, handle->init_data_lock_notification.cptr, &src);
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
    seL4_DebugNameThread(handle->main_thread->tcb.cptr, handle->name);
#endif
    handle->init_data.proc_name = (char *)proc_name; /* protobuf uses non const strings */
    handle->init_data.cnode_size_bits = handle->attrs.cnode_size_bits;
    handle->init_data.stack_size_pages = handle->attrs.stack_size_pages; 
    handle->init_data.stack_vaddr = (seL4_Word)handle->main_thread->stack_vaddr;


    return 0;
}


