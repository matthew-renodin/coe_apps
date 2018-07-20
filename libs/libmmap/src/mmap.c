/**
 * @file mmap.c
 * @brief Core implementation of libmmap
 */
#include <stdio.h>
#include <stdlib.h>

#include <sel4/sel4.h>
#include <vka/vka.h>
#include <vspace/vspace.h>
#include <utils/util.h>
#include <sel4utils/helpers.h>

#include <init/init.h>
#include <mmap/mmap.h>


const mmap_entry_attr_t mmap_attr_4k_code = {
    .page_size_bits  = PAGE_BITS_4K,
    .readable        = 1,
    .writable        = 0,
    .executable      = 1,
    .cacheable       = 1,
};

const mmap_entry_attr_t mmap_attr_4k_data = {
    .page_size_bits  = PAGE_BITS_4K,
    .readable        = 1,
    .writable        = 1,
    .executable      = 0,
    .cacheable       = 1,
};

const mmap_entry_attr_t mmap_attr_4k_readonly = {
    .page_size_bits  = PAGE_BITS_4K,
    .readable        = 1,
    .writable        = 0,
    .executable      = 0,
    .cacheable       = 1,
};

const mmap_entry_attr_t mmap_attr_4k_device = {
    .page_size_bits  = PAGE_BITS_4K,
    .readable        = 1,
    .writable        = 1,
    .executable      = 0,
    .cacheable       = 0,
};


static int remap_fix_executable_perms(seL4_CPtr page,
                                      seL4_CPtr vspace_root_cap,
                                      const mmap_entry_attr_t *attr)
{
    int error;
#ifdef CONFIG_ARCH_ARM
        if(!attr->executable) {
            seL4_ARCH_VMAttributes vm_attrs = attr->cacheable ? seL4_ARCH_Default_VMAttributes :
                                                        seL4_ARCH_Uncached_VMAttributes;
            vm_attrs |= seL4_ARM_ExecuteNever;

            seL4_CapRights_t rights = seL4_CapRights_new(false, attr->readable, attr->writable);

            error = seL4_ARCH_Page_Remap(page,
                                         vspace_root_cap,
                                         rights,
                                         vm_attrs);
            if(error) {
                ZF_LOGE("Failed to remap page");
                return -1;
            }
        }
#else
        ZF_LOGW_IF(!attr->executable,
                   "Unable to set the executable perms for non-ARM architectures");
#endif
    return 0;
}


static inline void* addr_at_page(void* addr, seL4_Word page, seL4_Word bits) {
    return (void*)((uintptr_t)addr + (page << bits));
}




int mmap_new_stack_custom(vspace_t *vspace,
                          seL4_CPtr vspace_root_cap,
                          seL4_Word num_pages,
                          void **vaddr,
                          reservation_t *res)
{
    int error;

    /* TODO: implement libsync */
    if(!init_check_initialized()) {
       ZF_LOGW("Init objects (vka, vspace) have not been setup.\n"
               "Run init_process or init_root_task to setup.");
       return -1;
    }

    if(vspace == NULL) {
        ZF_LOGE("Null vspace pointer passed.");
        return -2;
    }

    if(vaddr == NULL) {
        ZF_LOGE("Null vaddr pointer passed.");
        return -3;
    }

    const mmap_entry_attr_t *attr = &mmap_attr_4k_data;
    seL4_CapRights_t rights = seL4_CapRights_new(false, attr->readable, attr->writable);

    /**
     * Make reservation for our pages. Reserve an extra page for the gaurd.
     */
    *res = vspace_reserve_range(vspace,
                                (num_pages + 1) * BIT(attr->page_size_bits),
                                rights,
                                attr->cacheable,
                                vaddr);
    if(res->res == NULL || *vaddr == NULL) {
        ZF_LOGE("Failed to reserve space for the page mapping.");
        return -3;
    }

    /**
     * Don't allocate the stack gaurd page, start at 1.
     */
    for(int i = 1; i <= num_pages; i++) {
        vka_object_t frame_obj;
        error = vka_alloc_frame(&init_objects.vka,
                                attr->page_size_bits,
                                &frame_obj);
        if(error) {
            ZF_LOGE("Failed to allocate frame object. Do you have enough untyped memory?");
            return -4;
        }

        void *page_addr =  addr_at_page(*vaddr, i, attr->page_size_bits);

        /**
         * Map it into the page directory
         */
        error = vspace_map_pages_at_vaddr(vspace,
                                          &frame_obj.cptr,
                                          &frame_obj.ut,
                                          page_addr,
                                          1, /* map a page at a time */
                                          attr->page_size_bits,
                                          *res);
        if(error) {
            ZF_LOGE("Failed to map a page at %lu",
                    (long unsigned)page_addr);
            return -5;
        }


        error = remap_fix_executable_perms(frame_obj.cptr,
                                           vspace_root_cap,
                                           attr);
        if(error) {
            ZF_LOGE("Failed to set the executable permissions for %lu",
                    (long unsigned)page_addr);
            return -6;
        }
    }

    /**
     * We want to give the stack top back.
     */
    *vaddr = addr_at_page(*vaddr, num_pages + 1, attr->page_size_bits);
    return 0;
}


static int mmap_device_pages_custom(vspace_t *vspace,
                                    seL4_CPtr vspace_root_cap,
                                    void *paddr,
                                    seL4_Word num_pages,
                                    const mmap_entry_attr_t *attr,
                                    seL4_CPtr *caps,
                                    bool use_existing_caps,
                                    void **vaddr,
                                    reservation_t *res)
{
    int error;

    /* TODO: implement libsync */
    if(!init_check_initialized()) {
       ZF_LOGW("Init objects (vka, vspace) have not been setup.\n"
               "Run init_process or init_root_task to setup.");
       return -1;
    }

    if(vspace == NULL) {
        ZF_LOGE("Null vspace pointer passed.");
        return -2;
    }

    if(vaddr == NULL) {
        ZF_LOGE("Null vaddr pointer passed.");
        return -3;
    }
    
    if(caps == NULL && use_existing_caps) {
        ZF_LOGE("Null caps array passed");
        return -3;
    }

    seL4_CapRights_t rights = seL4_CapRights_new(false, attr->readable, attr->writable);

    /**
     * Make reservation for our pages
     */
    *res = vspace_reserve_range(vspace,
                                num_pages * BIT(attr->page_size_bits),
                                rights,
                                attr->cacheable,
                                vaddr);
    if(res->res == NULL || *vaddr == NULL) {
        ZF_LOGE("Failed to reserve space for the page mapping.");
        return -3;
    }

    for(int i = 0; i < num_pages; i++) {
        /**
         * 
         * Allocate a single frame, if null paddr is used, then we 
         * fallback to using any physical frame.
         * If "use_existing_frames" is set, then we just use the caps array.
         */
        vka_object_t frame_obj;
        if(use_existing_caps) {
            frame_obj.cptr = caps[i];
        } else if(paddr != NULL) { 
            seL4_Word current_paddr = (seL4_Word)paddr+(i << attr->page_size_bits);
            error = vka_alloc_object_at_maybe_dev(&init_objects.vka,
                                                  kobject_get_type(KOBJECT_FRAME,
                                                                   attr->page_size_bits),
                                                  attr->page_size_bits,
                                                  current_paddr,
                                                  true, /* can use device uts */
                                                  &frame_obj);
        } else {
            error = vka_alloc_frame(&init_objects.vka,
                                    attr->page_size_bits,
                                    &frame_obj);
        }
        if(error) {
            ZF_LOGE("Failed to allocate frame object. Do you have enough untyped memory?");
            return -4;
        }
        
        /**
         * If the caller passed a caps array we need to give the cap for this frame
         */
        if(caps != NULL && !use_existing_caps) {
            caps[i] = frame_obj.cptr;
        }

        /**
         * Map it into the page directory
         */
        error = vspace_map_pages_at_vaddr(vspace,
                                          &frame_obj.cptr,
                                          (use_existing_caps) ? NULL : &frame_obj.ut,
                                          addr_at_page(*vaddr, i, attr->page_size_bits),
                                          1, /* map a page at a time */
                                          attr->page_size_bits,
                                          *res);
        if(error) { /* TODO add more sophisticated error checking */
            ZF_LOGE("Failed to map a page at %lu",
                    (long unsigned)addr_at_page(*vaddr, i, attr->page_size_bits));
            return -5;
        }

        /**
         * This is a temporary solution to the fact that sel4utils/vspace doesn't
         * expose executable permissions.
         */
        error = remap_fix_executable_perms(frame_obj.cptr,
                                           vspace_root_cap,
                                           attr);
        if(error) {
            ZF_LOGE("Failed to set the executable permissions for %lu",
                    (long unsigned)addr_at_page(*vaddr, i, attr->page_size_bits));
            return -6;
        }

    }

    return 0;
}


/**
 * We will probably not do any lock/error checking here since
 * these are just convenience calls
 */

int mmap_new_pages(seL4_Word num_pages,
                   const mmap_entry_attr_t *attr,
                   void **vaddr,
                   reservation_t *res)
{

    return mmap_device_pages_custom(&init_objects.vspace,
                                    init_objects.page_dir_cap,
                                    NULL,
                                    num_pages,
                                    attr,
                                    NULL,
                                    false,
                                    vaddr,
                                    res);
}


int mmap_new_pages_custom(vspace_t *vspace,
                          seL4_CPtr vspace_root_cap,
                          seL4_Word num_pages,
                          const mmap_entry_attr_t *attr,
                          seL4_CPtr *caps,
                          void **vaddr,
                          reservation_t *res)
{
    return mmap_device_pages_custom(vspace,
                                    vspace_root_cap,
                                    NULL,
                                    num_pages,
                                    attr,
                                    caps,
                                    false,
                                    vaddr,
                                    res);
}


int mmap_new_device_pages_custom(vspace_t *vspace,
                                 seL4_CPtr vspace_root_cap,
                                 void *paddr,
                                 seL4_Word num_pages,
                                 const mmap_entry_attr_t *attr,
                                 seL4_CPtr *caps,
                                 void **vaddr,
                                 reservation_t *res)
{
    return mmap_device_pages_custom(vspace,
                                    vspace_root_cap,
                                    paddr,
                                    num_pages,
                                    attr,
                                    caps,
                                    false,
                                    vaddr,
                                    res);
}


int mmap_existing_pages_custom(vspace_t *vspace,
                               seL4_CPtr vspace_root_cap,
                               seL4_Word num_pages,
                               const mmap_entry_attr_t *attr,
                               seL4_CPtr *caps,
                               void **vaddr,
                               reservation_t *res)
{
    return mmap_device_pages_custom(vspace,
                                    vspace_root_cap,
                                    NULL,
                                    num_pages,
                                    attr,
                                    caps,
                                    true,
                                    vaddr,
                                    res);
}



