/**
 * @file connect.c
 * @brief Implementation of process configuration functions
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
#include <process/internal.h>


static int init_ep_obj(process_ep_conn_t *conn)
{
    libprocess_prologue();

    libprocess_check_arg(conn);

    libprocess_set_status(vka_alloc_endpoint(&init_objects.vka, &conn->vka_obj));
    libprocess_guard(libprocess_get_status(), -1, libprocess_epilogue, "Failed to alloc ep");

    libprocess_return_success();
    libprocess_epilogue();
}

static int init_notif_obj(process_ep_conn_t *conn)
{
    libprocess_prologue();
    libprocess_check_arg(conn);

    libprocess_set_status(vka_alloc_notification(&init_objects.vka, &conn->vka_obj));
    libprocess_guard(libprocess_get_status(), -1, libprocess_epilogue, "Failed to alloc notif");

    libprocess_return_success();
    libprocess_epilogue();
}


static int init_shmem_obj(process_shmem_conn_t *conn,
                          const process_conn_obj_attr_t *attr)
{
    int i;
    libprocess_prologue();
    libprocess_check_arg(conn);
    
    if(attr == NULL) {
        attr = &process_default_shmem_4k;
    }

    conn->num_pages = attr->num_pages;
    conn->page_bits = attr->page_bits;
    conn->self_mapped = false;

    conn->vka_obj_list = malloc(sizeof(vka_object_t)*conn->num_pages);
    libprocess_check_malloc(conn->vka_obj_list, libprocess_epilogue);

    for(i = 0; i < conn->num_pages; i++) {
        libprocess_set_status(vka_alloc_frame(&init_objects.vka,
                                              conn->page_bits,
                                              &conn->vka_obj_list[i]));

        libprocess_guard(libprocess_get_status(), -1, failed_alloc_frame,
                         "Failed to allocate a page of memory from vka");
    }

    libprocess_return_success();

failed_alloc_frame:
    for(i = i-1; i >= 0; i--) {
        vka_free_object(&init_objects.vka, &conn->vka_obj_list[i]);
    }
    free(conn->vka_obj_list);

    libprocess_epilogue();
}


static int init_conn_obj(process_conn_type_t typ,
                         const char *name,
                         const process_conn_obj_attr_t *attr,
                         process_conn_obj_t *obj)
{
    libprocess_prologue();

    obj->typ = typ;
    obj->name = name;
    obj->ref_count = 0;

    switch(typ) {
        case PROCESS_ENDPOINT:
            libprocess_set_status(init_ep_obj(&obj->obj.ep));
            break;
        case PROCESS_NOTIFICATION:
            libprocess_set_status(init_notif_obj(&obj->obj.notif));
            break;
        case PROCESS_SHARED_MEMORY:
            libprocess_set_status(init_shmem_obj(&obj->obj.shmem, attr));
            break;
        default:
            libprocess_guard(true, -1, libprocess_epilogue, "Invalid conn type");
    }
    libprocess_guard(libprocess_get_status(), -1, libprocess_epilogue, "Failed to init object");

    libprocess_return_success();
    libprocess_epilogue();
}


int process_create_conn_obj(process_conn_type_t typ,
                            const char *name,
                            const process_conn_obj_attr_t *attr,
                            process_conn_obj_t **obj)
{
    libprocess_prologue();
    libprocess_check_initialized();

    libprocess_guard(typ >= PROCESS_MAX_NUM_CONN_TYPES, -1, libprocess_epilogue,
                     "Invalid process conn type");
    libprocess_check_arg(name);
    libprocess_check_arg(obj);

    *obj = malloc(sizeof(process_conn_obj_t));
    libprocess_check_malloc(*obj, libprocess_epilogue);

    libprocess_set_status(init_conn_obj(typ, name, attr, *obj));
    libprocess_guard(libprocess_get_status(), -1, failed_init,
                     "Failed to initialize conn obj"); /*TODO init error code */

    libprocess_return_success();

failed_init:
    free(*obj);
    *obj = NULL;

    libprocess_epilogue();
}


static int cleanup_ep_obj(process_ep_conn_t *conn)
{
    libprocess_prologue();
    libprocess_check_arg(conn);

    vka_free_object(&init_objects.vka, &conn->vka_obj);

    libprocess_return_success();
    libprocess_epilogue();
}

static int cleanup_shmem_obj(process_shmem_conn_t *conn)
{
    libprocess_prologue();
    libprocess_check_arg(conn);

    if(conn->self_mapped) {
        vspace_unmap_pages(&init_objects.vspace,
                           conn->self_addr,
                           conn->num_pages,
                           conn->page_bits,
                           &init_objects.vka);
        vspace_free_reservation(&init_objects.vspace, conn->self_res);
    }

    for(int i = 0; i < conn->num_pages; i++) {
        vka_free_object(&init_objects.vka, &conn->vka_obj_list[i]);
    }

    free(conn->vka_obj_list);

    libprocess_return_success();
    libprocess_epilogue();
}


int process_free_conn_obj(process_conn_obj_t **obj)
{
    libprocess_prologue();
    libprocess_check_arg(obj);
    libprocess_check_arg(*obj);
    libprocess_guard((*obj)->ref_count > 0, -1, libprocess_epilogue,
                     "Cannot free object if child processes reference it.");
    
    switch((*obj)->typ) {
        case PROCESS_ENDPOINT:
            libprocess_set_status(cleanup_ep_obj(&(*obj)->obj.ep));
            break;
        case PROCESS_NOTIFICATION:
            libprocess_set_status(cleanup_ep_obj(&(*obj)->obj.notif));
            break;
        case PROCESS_SHARED_MEMORY:
            libprocess_set_status(cleanup_shmem_obj(&(*obj)->obj.shmem));
            break;
        default:
               libprocess_guard(true, -1, libprocess_epilogue, "Invalid conn type");
    }
    libprocess_guard(libprocess_get_status(), -1, libprocess_epilogue,
                     "Failed to cleanup conn obj");

    free(*obj);
    *obj = NULL;

    libprocess_return_success();
    libprocess_epilogue();
}




/**
 * Convenience function for copying ep and notifications to process
 * Assumes all arguments have been error checked
 * This function must acquire process lock because current_list_head is a critical resource
 */
static inline int copy_cptr_to_proc(process_handle_t *handle,
                                    seL4_CPtr ep_cap,
                                    seL4_CapRights_t perms,
                                    const char *conn_name,
                                    EndpointData **current_list_head) 
{
    libprocess_prologue();

    EndpointData *ep_data = malloc(sizeof(EndpointData));
    libprocess_guard(ep_data == NULL, -1, libprocess_epilogue,
                     "Failed to malloc Endpoint Data");

    endpoint_data__init(ep_data);
    ep_data->name = (char *)conn_name; /* protobuf uses non const strings */
    ep_data->cap = libprocess_copy_cap_next_slot(handle, ep_cap, perms);
    libprocess_guard(ep_data->cap == seL4_CapNull, -2, free_endpoint,
                     "Failed to copy ep cap");

    LINKED_LIST_PREPEND(ep_data, *current_list_head);
    libprocess_return_success();
    free_endpoint:
        free(ep_data);
    libprocess_epilogue();
}


/**
 * Assumes you have error checked args
 */
static int copy_ep_to_proc(process_handle_t *handle,
                           seL4_CPtr ep_cap,
                           seL4_CapRights_t perms,
                           const char *conn_name) 
{
    return copy_cptr_to_proc(handle, ep_cap, perms, conn_name,
                             &handle->init_data.ep_list_head);
}


/**
 * Assumes you have error checked args
 */
static int copy_notification_to_proc(process_handle_t *handle,
                                     seL4_CPtr ep_cap,
                                     seL4_CapRights_t perms,
                                     const char *conn_name) 
{
    return copy_cptr_to_proc(handle, ep_cap, perms, conn_name,
                             &handle->init_data.notification_list_head);
}



static int copy_shmem_generic(process_shmem_conn_t *conn,
                              process_conn_perms_t perms,
                              vspace_t *vspace,
                              seL4_CPtr page_dir,
                              reservation_t *res,
                              void **vaddr)
{   
    int i;
    AUTOFREE seL4_CPtr *caps = NULL;

    libprocess_prologue();

    libprocess_check_arg(conn);
    libprocess_check_arg(res);

    caps = malloc(sizeof(seL4_CPtr)*conn->num_pages);
    libprocess_check_malloc(caps, libprocess_epilogue);

    for(i = 0; i < conn->num_pages; i++) {
        cspacepath_t path1, path2;
        vka_cspace_make_path(&init_objects.vka, conn->vka_obj_list[i].cptr, &path1);
        vka_cspace_alloc_path(&init_objects.vka, &path2);
        libprocess_set_status(vka_cnode_copy(&path2, &path1, seL4_AllRights));
        libprocess_guard(libprocess_get_status(), -1, failed_copy_cap,
                         "Failed to copy cap for shared page.");
        caps[i] = path2.capPtr;
    }

    mmap_entry_attr_t map_attrs;
    map_attrs.readable = perms.r;
    map_attrs.writable = perms.w;
    map_attrs.executable = perms.x;
    map_attrs.page_size_bits = conn->page_bits;

    libprocess_set_status(mmap_existing_pages_custom(vspace,
                                                     page_dir,
                                                     conn->num_pages,
                                                     &map_attrs,
                                                     caps,
                                                     vaddr,
                                                     res));
    libprocess_guard(libprocess_get_status(), -1, failed_mmap,
                     "Failed to share pages to child process");

    libprocess_return_success();

failed_mmap:
failed_copy_cap:
    for(i = i-1; i >= 0; i--) {
        cspacepath_t cap_path;
        vka_cspace_make_path(&init_objects.vka, caps[i], &cap_path);
        vka_cnode_delete(&cap_path);
        vka_cspace_free(&init_objects.vka, caps[i]);
    }

    libprocess_epilogue();
}


static int copy_shmem_to_proc(process_handle_t *handle,
                              process_conn_obj_t *obj,
                              process_conn_perms_t perms)
{
    libprocess_prologue();

    libprocess_check_arg(handle);
    libprocess_check_arg(obj);
    libprocess_guard(obj->typ != PROCESS_SHARED_MEMORY, -1, libprocess_epilogue,
                     "Trying to map a non shmem object.");


    process_shmem_conn_t *conn = &obj->obj.shmem;

    SharedMemoryData *shmem_data = malloc(sizeof(SharedMemoryData));
    libprocess_check_malloc(shmem_data, libprocess_epilogue);

    void *vaddr;
    reservation_t res;
    libprocess_set_status(copy_shmem_generic(conn,
                                             perms,
                                             &handle->vspace,
                                             handle->page_dir.cptr,
                                             &res,
                                             &vaddr));
    libprocess_guard(libprocess_get_status(), -1, failed,
                     "Failed to copy shmem");

    shared_memory_data__init(shmem_data);
    shmem_data->name = (char *)obj->name; /* protobuf uses non const strings */
    shmem_data->addr = (seL4_Word)vaddr;
    shmem_data->length_bytes = conn->num_pages * BIT(conn->page_bits);

    LINKED_LIST_PREPEND(shmem_data, handle->init_data.shmem_list_head);
    libprocess_return_success();

failed:
    free(shmem_data);

    libprocess_epilogue();
}


static int connect_ep_self(process_ep_conn_t *conn,
                           seL4_CPtr *ret)
{
    libprocess_prologue();
    libprocess_check_arg(conn);
    libprocess_check_arg(ret);

    *ret = conn->vka_obj.cptr;

    libprocess_return_success();
    libprocess_epilogue();
}


static int connect_shmem_self(process_shmem_conn_t *conn,
                              process_conn_perms_t perms,
                              void **ret)
{
    libprocess_prologue();
    libprocess_check_arg(conn);
    libprocess_check_arg(ret);

    libprocess_guard(conn->self_mapped, -1, libprocess_epilogue,
                     "You cannot map this memory to self more than once");

    libprocess_set_status(copy_shmem_generic(conn,
                                             perms,
                                             &init_objects.vspace,
                                             init_objects.page_dir_cap,
                                             &conn->self_res,
                                             ret));
    libprocess_guard(libprocess_get_status(), -1, libprocess_epilogue, "Failed to copy shmem");

    conn->self_mapped = true;
    conn->self_addr = *ret;

    libprocess_return_success();
    libprocess_epilogue();

}

int process_connect(process_handle_t *handle,
                    process_conn_obj_t *obj,
                    process_conn_perms_t perms,
                    process_conn_ret_t *ret)
{
    libprocess_prologue();

    libprocess_check_initialized();
    libprocess_check_arg(obj);

    seL4_CapRights_t rights = seL4_CapRights_new(perms.g, perms.r, perms.w);

    switch(obj->typ) {
        case PROCESS_ENDPOINT:
            if(handle == PROCESS_SELF) {
                libprocess_set_status(connect_ep_self(&obj->obj.ep, &ret->self_cap));
            } else {
                libprocess_set_status(copy_ep_to_proc(handle,
                                                      obj->obj.ep.vka_obj.cptr,
                                                      rights,
                                                      obj->name));
            }
            break;
        case PROCESS_NOTIFICATION:
            if(handle == PROCESS_SELF) {
                libprocess_set_status(connect_ep_self(&obj->obj.ep, &ret->self_cap));
            } else {
                libprocess_set_status(copy_notification_to_proc(handle,
                                                                obj->obj.notif.vka_obj.cptr,
                                                                rights,
                                                                obj->name));
            }
            break;
        case PROCESS_SHARED_MEMORY:
            if(handle == PROCESS_SELF) {
                libprocess_set_status(connect_shmem_self(&obj->obj.shmem,
                                                         perms,
                                                         &ret->self_shmem_addr));
            } else {
                libprocess_set_status(copy_shmem_to_proc(handle,
                                                         obj,
                                                         perms));
            }
            break;
        default:
            libprocess_guard(true, -1, libprocess_epilogue, "Invalid conn type");
    }
    libprocess_guard(libprocess_get_status(), -1, libprocess_epilogue,
                     "Failed to connect");

    if(handle != PROCESS_SELF) { 
        process_shared_objects_ref_t *ref = malloc(sizeof(process_shared_objects_ref_t));
        libprocess_check_malloc(ref, libprocess_epilogue);
        ref->ref2 = obj;
        LINKED_LIST_PREPEND(ref, handle->shared_objects);
        obj->ref_count++;
    }


    libprocess_return_success();

    libprocess_epilogue();
}
