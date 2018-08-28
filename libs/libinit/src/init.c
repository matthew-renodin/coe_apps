/*
 * Copyright 2018, Intelligent Automation, Inc.
 * This software was developed in part under Air Force contract number FA8750-15-C-0066 and DARPA 
 *  contract number 140D6318C0001.
 * This software was released under DARPA, public release number 1.0.
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
 * @file init.c
 * @brief Core implementation of libinit
 *
 * A core goal of this library is to abstract away the root task details.
 * One important part of this goal is setting up the allocators/bookkeeping. These
 * allocator object operate at various layers with upper layers depending on lower
 * layers.
 *
 * +------------------------------------+------------------------------------+
 * | INTERFACE:                         | IMPLMENTATION:                     |
 * +------------------------------------+------------------------------------+
 * | vspace (virtual memory manager)    < sel4utils                          |
 * +------------------------------------+------------------------------------+
 * | vka (kernel object allocator)      < allocman                           |
 * +------------------------------------+------------------------------------+
 * 
 * libinit gathers these bookkeeping objects/structs into one place: init_objects.
 *
 * The main resource/currency in dynamic sel4 systems is "untyped" memory objects.
 * These objects represent unmapped physical memory that can be used for either
 * making kernel objects or can be used as frames for virtual memory. This means 
 * that allocman must be supplied with untyped memory objects that it can use.
 *
 * The root task gets all of the untyped memory objects at boot time; however,
 * other processes that are chilren/ancestors of root task must be explicitly
 * given untyped memory by their parent.
 *
 * +------------------------------+-----------------+
 * | ROOT TASK:                   | ANCESTORS:      |
 * +------------------------------+-----------------+
 * | init_objects allocators (vka, vspace)          |
 * +------------------------------+-----------------+
 * | simple (abstraction of b.i.) | init_data       |
 * +------------------------------+ (given untypeds |
 * | bootinfo (list of untypeds)  | from parent)    |
 * +------------------------------+-----------------+
 * 
 */

#include <autoconf.h>

#include <stdint.h>
#include <string.h>

#include <sel4/sel4.h>
#include <vka/vka.h>
#include <vspace/vspace.h>
#include <simple/simple.h>
#include <simple-default/simple-default.h>
#include <allocman/allocman.h>
#include <allocman/vka.h>
#include <allocman/bootstrap.h>
#include <sel4platsupport/bootinfo.h>
#include <sel4platsupport/platsupport.h>
#include <sel4platsupport/serial.h>
#include <utils/util.h>
#include <sel4utils/vspace.h>
#include <cpio/cpio.h>

#include <init/init.h>


/**
 * Global variable for the bookkeeping objects.
 */
init_objects_t init_objects = {0};


UNUSED static serial_objects_t serial_objects;

/**
 * Global variables from libsel4muslcsys
 * that allow us to define where malloc pulls memory from.
 * These variables require CONFIG_LIB_SEL4_MUSLC_SYS_MORECORE_BYTES==0
 */
#if CONFIG_LIB_SEL4_MUSLC_SYS_MORECORE_BYTES > 0
#error "Libinit requires CONFIG_LIB_SEL4_MUSLC_SYS_MORECORE_BYTES to be set to 0 for operation. Please reset this config option."
#endif
extern vspace_t *muslc_this_vspace;
extern reservation_t muslc_brk_reservation;
extern void *muslc_brk_reservation_start;
extern seL4_CPtr muslc_vspace_root_cap;
extern char *morecore_area;
extern size_t morecore_size;


/* In order for allocman to start bookkeeping it needs memory. 
 * This array is memory for it to bootstrap itself before untyped 
 * memory is accessable. After that it can allocate its own pages,
 * with a limit set by the dynamic pool size.
 */
static uint8_t allocman_static_pool[CONFIG_LIB_INIT_ALLOCMAN_STATIC_POOL_BYTES];

/* Static memory to help bootstrap the virtual memory bookkeeping */
static sel4utils_alloc_data_t vspace_bootstrap_data;

/* Reservation for our heap */
static sel4utils_res_t heap_res;

static void print_coe_banner(void) {
    printf("\n"
           "   __________  ____   _____     ____\n"
           "  / __/ __/ / / / /  / ___/__  / __/\n"
           " _\\ \\/ _// /_/_  _/ / /__/ _ \\/ _/  \n"
           "/___/___/____//_/   \\___/\\___/___/  \n\n");
    printf("Setting up root task.\n"); 
}

static void print_cpio_data(void) {
    /* The linker will link this symbol to the start address  *
     * of an archive of attached applications.                */
    extern char _cpio_archive[];

    if (_cpio_archive == NULL) return;

    printf("Parsing cpio data:\n");
    printf("+-------+------------------+------------+--------------+\n");
    printf("| index |        name      |  address   | size (bytes) |\n");
    printf("+-------+------------------+------------+--------------+\n");
    for(int i = 0;; i++) {
        unsigned long size;
        const char *name;
        void *data;

        data = cpio_get_entry(_cpio_archive, i, &name, &size);
        if(data != NULL){
            printf("| %3d   | %16s | %p | %12lu |\n", i, name, data, size);
        }else{
            break;
        }
    }
    printf("+------------------------------------------------------+\n");
}


/**
 * Allocman needs a static array/pool to bootstrap itself and the system.
 * We can give it a second, larger unmapped pool/space.
 *
 * Allocman will lazily map pages into this reserved space when it needs
 * space for bookkeeping.
 */
static int setup_allocman_dual_pool(seL4_Word pool_size) {
    reservation_t reservation;
    void * allocman_dynamic_pool;

    /*
     * Reserve a new big space for allocman using vspace.
     */
    reservation = vspace_reserve_range(&init_objects.vspace,
                                       pool_size,
                                       seL4_AllRights,
                                       1, /* Cacheable */
                                       &allocman_dynamic_pool);
    if(reservation.res == NULL) {
        ZF_LOGW("Failed to reserve a chunk of memory for allocman");
        return -1;
    }

    /*
     * Use the newly reserved space as bookkeeping space for allocman.
     */
    bootstrap_configure_virtual_pool(init_objects.allocman, 
                                     allocman_dynamic_pool,
                                     pool_size,
                                     init_objects.page_dir_cap);
    return 0;
}

/**
 * The root task is by default mapped in with all pages set to RWX.
 * This function remaps these pages to be either code (RX) or data (RW).
 */
static int remap_root_task_elf_regions() {
    int error;
#ifdef CONFIG_ARCH_ARM
    extern char __executable_start[];
    extern char _etext[];
    extern char _edata[];
    extern char _end[];

/*
    ZF_LOGD("Remapping root task image... start:%p, etext:%p, edata:%p, end:%p",
            __executable_start,
            _etext,
            _edata,
            _end);
*/

    int num_image_caps = simple_get_userimage_count(&init_objects.simple);
    seL4_CPtr page_dir = simple_get_init_cap(&init_objects.simple, seL4_CapInitThreadVSpace);

    /**
     * We have to assume here that the image is contiguous in both physical and 
     * virtual memory. This is pretty brittle, but the only way at the moment.
     */
    uintptr_t phys_start = seL4_ARM_Page_GetAddress(simple_get_nth_userimage(
                                                                        &init_objects.simple,
                                                                        0)).paddr;
    ptrdiff_t offset = (uintptr_t)__executable_start - phys_start;

    for(int i = 0; i < num_image_caps; i++) {
        seL4_CPtr image_frame = simple_get_nth_userimage(&init_objects.simple, i);
        uintptr_t paddr = seL4_ARM_Page_GetAddress(image_frame).paddr;
        uintptr_t vaddr = paddr + offset;

        if(vaddr >= ROUND_DOWN((uintptr_t)__executable_start, PAGE_SIZE_4K) &&
           vaddr < ROUND_UP((uintptr_t)_etext, PAGE_SIZE_4K))
        {
            error = seL4_ARCH_Page_Remap(image_frame,
                                         page_dir,
                                         seL4_CanRead,
                                         seL4_ARCH_Default_VMAttributes);
            if(error) {
                ZF_LOGE("Failed to remap text page");
                return -1;
            }
        } else if(vaddr >= ROUND_UP((uintptr_t)_etext, PAGE_SIZE_4K) &&
                  vaddr < ROUND_UP((uintptr_t)_end, PAGE_SIZE_4K))
        {
            error = seL4_ARCH_Page_Remap(image_frame,
                                         page_dir,
                                         seL4_ReadWrite,
                                         seL4_ARCH_Default_VMAttributes | seL4_ARM_ExecuteNever);
            if(error) {
                ZF_LOGE("Failed to remap text page");
                return -2;
            }

        }
    }
#endif
    return 0;
}


int init_process(void) {
    int error;
    int i,j;
    static int run_once = 0;

    if(run_once) {
        ZF_LOGE("This function may only be called once");
        return -1;
    }
    run_once = 1;

    if(init_objects.initialized) {
        ZF_LOGE("Init objects have already been initialized.");
        return -2;
    }
    
    /**
     * Information about the init_data and heap are passed as environment variables
     */
    morecore_area = (void*)strtol(getenv("HEAP_ADDR"), NULL, 16);
    morecore_size = atoi(getenv("HEAP_SIZE"));
    
    /* Malloc is available now */

    void *init_data_packed = (void*)strtol(getenv("INIT_DATA_ADDR"), NULL, 16);
    seL4_Word init_data_packed_size = atoi(getenv("INIT_DATA_SIZE"));

    //ZF_LOGD("env: HEAP_ADDR=%p", morecore_area);
    //ZF_LOGD("env: HEAP_SIZE=%lu", (unsigned long) morecore_size);
    //ZF_LOGD("env: INIT_DATA_ADDR=%p", init_data_packed);
    //ZF_LOGD("env: INIT_DATA_SIZE=%lu", (unsigned long) init_data_packed_size);


    /**
     *  Make use of init objects as thread safe as possible
     */
    init_lock_init(INIT_CHILD_INIT_OBJECTS_LOCK_SLOT);
    
    /**
     * Unpack the init data from our parent process.
     */
    init_lock_objects();
    init_objects.init_data = init_data__unpack(NULL, 
                                               init_data_packed_size,
                                               init_data_packed);
    
    /**
     * Setup the root_task/ancestor abstraction
     */
    init_objects.cnode_cap = INIT_CHILD_CNODE_SLOT;
    init_objects.page_dir_cap = INIT_CHILD_PAGE_DIR_SLOT;
    init_objects.tcb_cap = INIT_CHILD_TCB_SLOT;
    init_objects.fault_cap = INIT_CHILD_FAULT_EP_SLOT;
    init_objects.asid_pool_cap = INIT_CHILD_ASID_POOL_SLOT;
    init_objects.sync_notification_cap = INIT_CHILD_SYNC_NOTIFICATION_SLOT;
    init_objects.process_lock_cap = INIT_CHILD_PROCESS_LOCK_SLOT;
    init_objects.thread_lock_cap = INIT_CHILD_THREAD_LOCK_SLOT;
    init_objects.proc_name = init_objects.init_data->proc_name;
    init_objects.initialized = 1;

#ifdef CONFIG_DEBUG_BUILD
    seL4_DebugNameThread(INIT_CHILD_TCB_SLOT, init_objects.proc_name);
#endif

    zf_log_set_tag_prefix(init_objects.proc_name);
    //ZF_LOGD("Starting up process: %s.", init_objects.proc_name);

    /**
     * Setup allocman. We are giving allocman a static array of memory to get
     * started with it's bookkeeping of untyped/other objects
     */
    init_objects.allocman = bootstrap_use_current_1level(
                                                  init_objects.cnode_cap,
                                                  init_objects.init_data->cnode_size_bits,
                                                  init_objects.init_data->cnode_next_free,
                                                  BIT(init_objects.init_data->cnode_size_bits),
                                                  CONFIG_LIB_INIT_ALLOCMAN_STATIC_POOL_BYTES,
                                                  allocman_static_pool);
    ZF_LOGF_IF(init_objects.allocman == NULL, "Failed to bootstrap allocman.");

    /**
     * Build the vka interface to our allocman.
     * Sets up function pointers.
     */
    allocman_make_vka(&init_objects.vka, init_objects.allocman);

    /* Surround Allocman with LockVKA */
    sync_mutex_init(&init_objects.vka_lock, INIT_CHILD_VKA_LOCK_SLOT);
    lockvka_replace(&init_objects.lockvka, &init_objects.vka, sync_mutex_make_interface(&init_objects.vka_lock));

    /**
     * Parse untypeds
     */
    seL4_Word total_ut_memory = 0;
    seL4_Word total_ut_count = 0;
    UntypedData *iter = init_objects.init_data->untyped_list_head;
    while(iter != NULL) {
        /**
         * Give our untyped objects to allocman
         */
        cspacepath_t path;
        vka_cspace_make_path(&init_objects.vka, iter->cap, &path);
        size_t ut_size = (size_t)iter->size;

        error = allocman_utspace_add_uts(init_objects.allocman,
                                         1,
                                         &path,
                                         &ut_size,
                                         (uintptr_t *)&iter->phys_addr, /* TODO optional! */
                                         ALLOCMAN_UT_KERNEL);
        ZF_LOGF_IF(error, "Failed to add untyped");
        total_ut_memory += (1lu << iter->size);
        total_ut_count++;

        iter = iter->next;
    }
    //ZF_LOGV("Added %lu untyped objects to allocman, totalling: %luK",
    //        (unsigned long) total_ut_count,
    //        (unsigned long) total_ut_memory / 1024);
    
    /**
     * At this point, we have untypeds and the VKA to use them
     **/
    init_objects.has_untypeds = (total_ut_count > 0) ? 1 : 0;

    /**
     * Setup an existing frames list.
     */
    seL4_Word init_data_pages = ROUND_UP(init_data_packed_size, PAGE_SIZE_4K);
    seL4_Word num_frames = init_objects.init_data->stack_size_pages +
                           init_data_pages +
                           (morecore_size / PAGE_SIZE_4K) + /* TODO assuming 4k alignment */
                           1; /* IPC buffer */

    SharedMemoryData *shmem_iter = init_objects.init_data->shmem_list_head;
    DeviceMemoryData *devmem_iter = init_objects.init_data->devmem_list_head;
    
    /* Count num frames */
    while(shmem_iter != NULL) {
        num_frames++;
        shmem_iter = shmem_iter->next;
    } 
    while(devmem_iter != NULL) {
        num_frames++;
        devmem_iter = devmem_iter->next;
    }
    shmem_iter = init_objects.init_data->shmem_list_head;
    devmem_iter = init_objects.init_data->devmem_list_head;

    /**
     * Allocate an array to hold existing frame addresses.
     * sel4utils expects a null terminated list, so +1
     */
    void **existing_frames = calloc((num_frames + 1), sizeof(void *));
    if(existing_frames == NULL) {
        ZF_LOGE("Failed to get memory for existing frames buffer");
        init_unlock_objects();
        return -3;
    }

    /**
     * Initialize the existing_frames array
     */
    j = 0;
    for(i = 0; i < init_data_pages; i++, j++) {
        existing_frames[j] = (void*)((uintptr_t)init_data_packed + (i << PAGE_BITS_4K));
    }
    for(i = 0; i < morecore_size / PAGE_SIZE_4K; i++, j++) {
        existing_frames[j] = (void*)((uintptr_t)morecore_area + (i << PAGE_BITS_4K));
    }
    for(i = 0; i < init_objects.init_data->stack_size_pages; i++, j++) {
        /**
         * This stack_vaddr points to the top of the stack so we have to subtract.
         * I am unsure if this works in the edge cases. TODO test.
         */
        existing_frames[j] = (void*)((uintptr_t)init_objects.init_data->stack_vaddr -
                                                (i << PAGE_BITS_4K));
    }

    while(shmem_iter != NULL) {
        ZF_LOGW_IF(shmem_iter->length_bytes%PAGE_SIZE_4K != 0, "Invalid length of shmem");

        for(i = 0; i < (shmem_iter->length_bytes/PAGE_SIZE_4K); i++, j++) {
            existing_frames[j] = (void*)((uintptr_t)shmem_iter->addr + (i << PAGE_BITS_4K));
        }
        shmem_iter = shmem_iter->next;
    } 
    while(devmem_iter != NULL) {
        for(i = 0; i < devmem_iter->num_pages; i++, j++) {
            existing_frames[j] = (void*)((uintptr_t)devmem_iter->virt_addr +
                                                    (i << devmem_iter->size_bits));
        }
        devmem_iter = devmem_iter->next;
    }
    existing_frames[j++] = (void *)seL4_GetIPCBuffer();

    ZF_LOGW_IF(j != num_frames, "Not all of the existing frames were copied.");


    if(total_ut_memory > 0) { /** TODO find a better number here */
        /**
         * Setup vspace object.
         */
        error = sel4utils_bootstrap_vspace(&init_objects.vspace,
                                           &vspace_bootstrap_data,
                                           init_objects.page_dir_cap,
                                           &init_objects.vka,
                                           NULL,
                                           NULL,
                                           (void**)existing_frames);
        free(existing_frames);
        if(error) {
            ZF_LOGE("Failed to setup vspace object");
            init_unlock_objects();
            return -4;
        }
        
    }

    /**
     * Surround VSpace with LockVSpace 
     **/
    sync_recursive_mutex_init(&init_objects.vspace_lock, INIT_CHILD_VSPACE_LOCK_SLOT);
    lockvspace_replace(&init_objects.lockvspace, &init_objects.vspace, sync_recursive_mutex_make_interface(&init_objects.vspace_lock));

    /**
     * At this point all the objects are initialized.
     *
     * If we have enough untypeds we can try to reserve extra space for bookkeeping.
     */
    if(total_ut_memory > CONFIG_LIB_INIT_ALLOCMAN_DYNAMIC_POOL_BYTES) {

        error = setup_allocman_dual_pool(CONFIG_LIB_INIT_ALLOCMAN_DYNAMIC_POOL_BYTES);
        if(error) {
            ZF_LOGE("Failed to set dual pool."
                    "Make sure you have enough size for the allocman static pool.");
        }

    } else if(total_ut_memory > 0) {
        ZF_LOGW("Warning: We have some untyped memory, but not enough to make a second pool"
                "for allocman. You may run out of bookkeeping space and fail to allocate"
                "objects in the future.");
    }

    error = init_set_thread_local_storage(NULL);
    if(error) {
        ZF_LOGE("Failed to set thread local storage");
        init_unlock_objects();
        return -5;
    }

    init_unlock_objects();
    return 0;
}




int init_root_task(void) {
    int error;
    static int run_once = 0;

    if(run_once) {
        ZF_LOGE("This function may only be called once");
        return -1;
    }
    run_once = 1;

    if(init_objects.initialized) {
        ZF_LOGE("Init objects have already been initialized.");
        return -2;
    }

    init_objects.proc_name = "root_task";

    zf_log_set_tag_prefix(init_objects.proc_name);

#ifdef CONFIG_DEBUG_BUILD
    seL4_DebugNameThread(seL4_CapInitThreadTCB, init_objects.proc_name);
#endif

    init_objects.info = platsupport_get_bootinfo();
    if(init_objects.info == NULL) {
        ZF_LOGE("Failed to get bootinfo.");
        return -3;
    }

    /* Initialize the simple structure's function pointers,
     * Simple manages our bootinfo struct for us. */
    simple_default_init_bootinfo(&init_objects.simple, init_objects.info);

    /* Create the bootinfo abstraction layer */
    init_objects.asid_control_cap = simple_get_init_cap(&init_objects.simple, seL4_CapASIDControl);
    init_objects.asid_pool_cap = simple_get_init_cap(&init_objects.simple, seL4_CapInitThreadASIDPool);
    init_objects.tcb_cap = simple_get_init_cap(&init_objects.simple, seL4_CapInitThreadTCB);
    init_objects.cnode_cap = simple_get_init_cap(&init_objects.simple, seL4_CapInitThreadCNode);
    init_objects.page_dir_cap = simple_get_init_cap(&init_objects.simple, seL4_CapInitThreadVSpace);
    init_objects.fault_cap = seL4_CapNull;


    init_objects.initialized = 1;
    /**
     * Remap the root task code and data sections to patch the executable permissions.
     */
    error = remap_root_task_elf_regions();
    if(error) {
        ZF_LOGE("Failed to remap elf regions to correct RWX perms");
        return -4; /* TODO: Should we just continue? */
    }

    /**
     * Setup allocman with a static pool to bootstrap its bookkeeping.
     * Since we are providing it our simple struct it will fill itself
     * with untyped memory chunks from the bootinfo.
     */
    init_objects.allocman = bootstrap_use_current_simple(
                                            &init_objects.simple, 
                                            CONFIG_LIB_INIT_ALLOCMAN_STATIC_POOL_BYTES,
                                            allocman_static_pool);
    if(init_objects.allocman == NULL) {
        ZF_LOGE("Failed to bootstrap allocman.");
        return -5;
    }

    /**
     * Build the vka interface to our allocman.
     * Sets up function pointers.
     */
    allocman_make_vka(&init_objects.vka, init_objects.allocman);

    /* Surround Allocman VKA with LOCKVKA before VSpace is initialized */
    vka_object_t vka_lock_notification;
    error = vka_alloc_notification(&init_objects.vka, &vka_lock_notification);
    if(error) {
        ZF_LOGE("Failed to allocate notification object.");
        return error;
    }
    sync_mutex_init(&init_objects.vka_lock, vka_lock_notification.cptr);
    lockvka_replace(&init_objects.lockvka, &init_objects.vka, sync_mutex_make_interface(&init_objects.vka_lock));

    /* At this point we are a process with untyped memory and the vka to use it */
    init_objects.has_untypeds = 1;

    /* 
     * Setup the vspace object. This bookkeeps/manages the virtual memory mappings.
     */
    error = sel4utils_bootstrap_vspace_with_bootinfo_leaky(&init_objects.vspace, 
                                                           &vspace_bootstrap_data,
                                                           init_objects.page_dir_cap,
                                                           &init_objects.vka,
                                                           init_objects.info);
    if(error) {
        ZF_LOGE("Failed to bootstrap vspace");
        return -6;
    }

    // /* Surround VSpace with LockVSpace */
    vka_object_t vspace_lock_notification;
    error = vka_alloc_notification(&init_objects.vka, &vspace_lock_notification);
    if(error) {
        ZF_LOGE("Failed to allocate notification object.");
        return error;
    }
    sync_recursive_mutex_init(&init_objects.vspace_lock, vspace_lock_notification.cptr);
    lockvspace_replace(&init_objects.lockvspace, &init_objects.vspace, sync_recursive_mutex_make_interface(&init_objects.vspace_lock));

    vka_object_t process_lock_notification;
    error = vka_alloc_notification(&init_objects.vka, &process_lock_notification);
    if(error) {
        ZF_LOGE("Failed to allocate notification object.");
        return error;
    }
    init_objects.process_lock_cap = process_lock_notification.cptr;

    vka_object_t thread_lock_notification;
    error = vka_alloc_notification(&init_objects.vka, &thread_lock_notification);
    if(error) {
        ZF_LOGE("Failed to allocate notification object.");
        return error;
    }
    init_objects.thread_lock_cap = thread_lock_notification.cptr;


    /**
     * Setup malloc to refill from our new allocators.
     * Malloc won't work before this point so we have to use reserve_range_no_alloc
     */
    lockvspace_lock(&init_objects.vspace, &init_objects.lockvspace);
    error = sel4utils_reserve_range_no_alloc(&init_objects.lockvspace.parent_vspace,
                                             &heap_res,
                                             CONFIG_LIB_INIT_ROOT_TASK_HEAP_SPACE,
                                             seL4_ReadWrite,
                                             1, /* Cacheable */
                                             &muslc_brk_reservation_start);
    lockvspace_unlock(&init_objects.vspace, &init_objects.lockvspace);
    if(error) {
        ZF_LOGE("Failed to reserve range for heap");
        return -7;
    }
    muslc_brk_reservation.res = &heap_res;
    muslc_this_vspace = &init_objects.vspace;
    muslc_vspace_root_cap = init_objects.page_dir_cap;

    /* Malloc is available now */

    /* At this point all the objects are initialized, but we want to give 
     * allocman more memory for bookkeeping, so we will dynamically allocate
     * some memory for it.
     */
    error = setup_allocman_dual_pool(CONFIG_LIB_INIT_ALLOCMAN_DYNAMIC_POOL_BYTES);
    if(error) {
        ZF_LOGE("Failed to set dual pool."
                "Make sure you have enough size for the allocman static pool.");
        return -8;
    }


    /* All the allocator layers are now configured. 
     * For release builds we need to setup the serial driver.
     * Since we want to control the serial caps/objects explicitly, we break the
     * initialization into two calls.
     * TODO: implement a solution here
     */
    //sel4platsupport_init_default_serial_caps(&vka, &vspace, &simple, &serial_objects);
    platsupport_serial_setup_simple(&init_objects.vspace, &init_objects.simple, &init_objects.vka);



    print_coe_banner();

    /* This will print the available untypeds */
    simple_print(&init_objects.simple);

    print_cpio_data();

    /**
     * Setup a notification for init lock to use
     */
    vka_object_t init_lock_notification;
    error = vka_alloc_notification(&init_objects.vka, &init_lock_notification);
    if(error) {
        ZF_LOGE("Failed to allocate notification object.");
        return error;
    }
    init_lock_init(init_lock_notification.cptr);

    /**
     * Setup a notification for libsync to use.
     */
    vka_object_t sync_notification;
    error = vka_alloc_notification(&init_objects.vka, &sync_notification);
    if(error) {
        ZF_LOGE("Failed to allocate notification object.");
        return -9;
    }
    init_objects.sync_notification_cap = sync_notification.cptr;

    error = init_set_thread_local_storage(NULL);
    if(error) {
        ZF_LOGE("Failed to set thread local storage");
        return -10;
    }

    return 0;
}




