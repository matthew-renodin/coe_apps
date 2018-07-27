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


static int init_conn_obj(process_conn_type_t typ,
                         const char *name,
                         process_conn_obj_attr_t *attr,
                         process_conn_obj_t *obj)
{
    libprocess_prologue();

    obj->typ = typ;
    obj->name = name;
    obj->ref_count = 0;

    switch(typ) {
        case PROCESS_ENDPOINT:
            init_ep_obj(&obj->obj.ep);
            break;
        case PROCESS_NOTIFICATION:
            init_notif_obj(&obj->obj.notif);
            break;
        case PROCESS_SHARED_MEMORY:
            ZF_LOGF("UNIMPLEMENTED");
            break;
        default:
            libprocess_guard(true, -1, libprocess_epilogue, "Invalid conn type");
    }

    libprocess_return_success();
    libprocess_epilogue();
}


int process_create_conn_obj(process_conn_type_t typ,
                            const char *name,
                            process_conn_obj_attr_t *attr,
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

    vka_free_object(&init_objects.vka, &conn->vka_obj);

    libprocess_return_success();
    libprocess_epilogue();
}


process_free_conn_obj(process_conn_obj_t **obj)
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
            ZF_LOGF("UNIMPLEMENTED");
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




int process_connect(process_handle_t *handle,
                    process_conn_obj_t *obj,
                    process_conn_perms_t perms)
{
    libprocess_prologue();

    libprocess_check_initialized();
    libprocess_check_arg(obj);

    seL4_CapRights_t rights = seL4_CapRights_new(perms.g, perms.r, perms.w);

    if(handle == PROCESS_SELF) { 
        ZF_LOGF("UNIMPLEMENTED");
    } else {
        switch(obj->typ) {
            case PROCESS_ENDPOINT:
                copy_ep_to_proc(handle, obj->obj.ep.vka_obj.cptr, rights, obj->name);
                break;
            case PROCESS_NOTIFICATION:
                copy_notification_to_proc(handle, obj->obj.notif.vka_obj.cptr, rights, obj->name);
                break;
            case PROCESS_SHARED_MEMORY:
                ZF_LOGF("UNIMPLEMENTED");
                break;
            default:
                libprocess_guard(true, -1, libprocess_epilogue, "Invalid conn type");
        }

        process_shared_objects_ref_t *ref = malloc(sizeof(process_shared_objects_ref_t));
        libprocess_check_malloc(ref, libprocess_epilogue);
        ref->ref2 = obj;
        LINKED_LIST_PREPEND(ref, handle->shared_objects);
    }

    obj->ref_count++;

    libprocess_return_success();

    libprocess_epilogue();
}
