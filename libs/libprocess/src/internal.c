/**
 * @file config.c
 * @brief Implementation of process configuration functions
 *
 */
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

/**
 *  Convenience function assumes valid args and does not worry about locking 
 **/
void libprocess_next_free_path(cspacepath_t *dst, process_handle_t *handle) {
    dst->root = handle->cnode.cptr;
    dst->capDepth = handle->attrs.cnode_size_bits;
    dst->capPtr = handle->cnode_next_free;
}


/**
 * Assumes you have error checked args
 */
seL4_CPtr libprocess_copy_cap_next_slot(process_handle_t *handle,
                                               seL4_CPtr new_cap,
                                               seL4_CapRights_t perms)
{
    libprocess_prologue();
    seL4_Word slot = seL4_CapNull;

    cspacepath_t dst, src;
    libprocess_next_free_path(&dst, handle);

    vka_cspace_make_path(&init_objects.vka, new_cap, &src);
    libprocess_set_status(vka_cnode_copy(&dst, &src, perms));
    libprocess_guard(libprocess_get_status(), -1, libprocess_epilogue, 
                     "Failed to copy cap into child cnode.");
    slot = handle->cnode_next_free++;
    
    libprocess_custom_epilogue()
    libprocess_return_value(libprocess_get_status() == 0 ? slot : seL4_CapNull);
}

/**
 * Assumes you have error checked args
 */
int libprocess_delete_cap_last_slot(process_handle_t *handle)
{
    libprocess_prologue();

    --handle->cnode_next_free;
    
    cspacepath_t dst;
    libprocess_next_free_path(&dst, handle);
    seL4_CNode_Delete(dst.root, dst.capPtr, dst.capDepth);

    libprocess_return_value(libprocess_get_status());
}




