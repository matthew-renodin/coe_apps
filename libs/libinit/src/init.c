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
 * @file init.c
 * @brief Core implementation of libinit
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



/* Internal bookeeping variables for a process
 * These object operate at various layers 
 * with upper layers depending on lower layers:
 *
 * ROOT TASK:
 * +------------------------------------+
 * | vspace (virtual memory manager)    |
 * +------------------------------------+
 * | vka (kernel object allocator)      |
 * +------------------------------------+
 * | allocman (untyped object manager)  |
 * +------------------------------------+
 * | simple (abstraction of bootinfo)   |  
 * +------------------------------------+
 * | bootinfo (list of untyped caps)    |
 * +------------------------------------+
 *
 * OTHER PROCESSES:
 * +------------------------------------+
 * | vspace (virtual memory manager)    |
 * +------------------------------------+
 * | vka (kernel object allocator)      |
 * +------------------------------------+
 * | allocman (untyped object manager)  |
 * +------------------------------------+
 * | caps to untyped objects            |
 * +------------------------------------+
 */

/**
 * Global variable for the bookkeeping objects.
 */
init_objects_t init_objects;

UNUSED static serial_objects_t serial_objects;

/**
 * Global variables from libsel4muslcsys
 * that allow us to define where malloc pulls memory from.
 */
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
static void *  allocman_dynamic_pool;

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

    printf("Parsing cpio data:\n");
    printf("--------------------------------------------------------\n");
    printf("| index |        name      |  address   | size (bytes) |\n");
    printf("|------------------------------------------------------|\n");
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
    printf("--------------------------------------------------------\n");
}


int init_process(void) {
    int error;
    int i,j;
    static int run_once = 0;

    if(run_once) {
        ZF_LOGF("This function may only be called once");
    }
    run_once = 1;
    
    /**
     * Information about the init_data and heap are passed as environment variables
     */
    morecore_area = (void*)strtol(getenv("HEAP_ADDR"), NULL, 16);
    morecore_size = atoi(getenv("HEAP_SIZE"));

    void *init_data_packed = (void*)strtol(getenv("INIT_DATA_ADDR"), NULL, 16);
    seL4_Word init_data_packed_size = atoi(getenv("INIT_DATA_SIZE"));

    ZF_LOGD("env: HEAP_ADDR=%p", morecore_area);
    ZF_LOGD("env: HEAP_SIZE=%lu", morecore_size);
    ZF_LOGD("env: INIT_DATA_ADDR=%p", init_data_packed);
    ZF_LOGD("env: INIT_DATA_SIZE=%lu", init_data_packed_size);

    memset(&init_objects, 0, sizeof(init_objects));
    /**
     * Unpack the init data from our parent process.
     */
    init_objects.init_data = init_data__unpack(NULL, 
                                               init_data_packed_size,
                                               init_data_packed);
    
    init_objects.cnode_cap = INIT_CHILD_CNODE_SLOT;
    init_objects.page_dir_cap = INIT_CHILD_PAGE_DIR_SLOT;
    init_objects.tcb_cap = INIT_CHILD_TCB_SLOT;
    init_objects.fault_cap = INIT_CHILD_FAULT_EP_SLOT;

#ifdef CONFIG_DEBUG_BUILD
    seL4_DebugNameThread(INIT_CHILD_TCB_SLOT, init_objects.init_data->proc_name);
#endif

    zf_log_set_tag_prefix(init_objects.init_data->proc_name);
    ZF_LOGD("Starting up process: %s.", init_objects.init_data->proc_name);


    /**
     * Setup allocman and vka bookkeeping objects
     * We are giving allocman a static array of memory to get started.
     */
    init_objects.allocman = bootstrap_use_current_1level(
                                                  INIT_CHILD_CNODE_SLOT,
                                                  init_objects.init_data->cnode_size_bits,
                                                  init_objects.init_data->cnode_next_free,
                                                  (1u << init_objects.init_data->cnode_size_bits),
                                                  CONFIG_LIB_INIT_ALLOCMAN_STATIC_POOL_BYTES,
                                                  allocman_static_pool);
    ZF_LOGF_IF(init_objects.allocman == NULL, "Failed to bootstrap allocman.");

    allocman_make_vka(&init_objects.vka, init_objects.allocman);

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

        error = allocman_utspace_add_uts(init_objects.allocman,
                                         1,
                                         &path,
                                         &iter->size,
                                         (uintptr_t *)&iter->phys_addr, /* TODO optional! */
                                         ALLOCMAN_UT_KERNEL);
        ZF_LOGF_IF(error, "Failed to add untyped");
        total_ut_memory += (1lu << iter->size);
        total_ut_count++;

        iter = iter->next;
    }
    ZF_LOGV("Added %lu untyped objects to allocman, totalling: %luK",
            total_ut_count,
            total_ut_memory / 1024);


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

    /* Allocate an extra space for the null terminator */
    void **existing_frames = calloc((num_frames + 1), sizeof(void *));
    if(existing_frames == NULL) {
        ZF_LOGE("Failed to get memory for existing frames buffer");
        return -1;
    }

    /**
     * Initialize the existing_frames array
     */
    j = 0;
    for(i = 0; i < init_data_pages; i++, j++) {
        existing_frames[j] = (void*)(init_data_packed + (i << PAGE_BITS_4K));
    }
    for(i = 0; i < morecore_size / PAGE_SIZE_4K; i++, j++) {
        existing_frames[j] = (void*)(morecore_area + (i << PAGE_BITS_4K));
    }
    for(i = 0; i < init_objects.init_data->stack_size_pages; i++, j++) {
        /**
         * This stack_vaddr points to the top of the stack so we have to subtract.
         * I am unsure if this works in the edge cases. TODO test.
         */
        existing_frames[j] = init_objects.init_data->stack_vaddr - (i << PAGE_BITS_4K);
    }

    while(shmem_iter != NULL) {
        ZF_LOGW_IF(shmem_iter->length_bytes%PAGE_SIZE_4K != 0, "Invalid length of shmem");

        for(i = 0; i < (shmem_iter->length_bytes/PAGE_SIZE_4K); i++, j++) {
            existing_frames[j] = shmem_iter->addr + (i << PAGE_BITS_4K);
        }
        shmem_iter = shmem_iter->next;
    } 
    while(devmem_iter != NULL) {
        for(i = 0; i < devmem_iter->num_pages; i++, j++) {
            existing_frames[j] = devmem_iter->virt_addr + (i << devmem_iter->size_bits);
        }
        devmem_iter = devmem_iter->next;
    }
    existing_frames[j++] = seL4_GetIPCBuffer();

    ZF_LOGW_IF(j != num_frames, "Not all of the existing frames were copied.");

    /**
     * Setup vspace object
     */
    error = sel4utils_bootstrap_vspace(&init_objects.vspace,
                                       &vspace_bootstrap_data,
                                       init_objects.page_dir_cap,
                                       &init_objects.vka,
                                       NULL,
                                       NULL,
                                       (void**)existing_frames);
    if(error) {
        ZF_LOGE("Failed to setup vspace object");
        return -2;
    }


    /**
     * If we have untypeds we can reserve space for bookkeeping.
     */
    if(init_objects.init_data->untyped_list_head != NULL) {
        /* At this point all the objects are initialized, but we want to give 
         * allocman more memory for bookkeeping, so we will reserve some memory for it.
         * It will do the actual allocation.
         */
    
        reservation_t reservation;
    
        /* Reserve a new big space for using vspace. */
        reservation = vspace_reserve_range(&init_objects.vspace,
                                           CONFIG_LIB_INIT_ALLOCMAN_DYNAMIC_POOL_BYTES,
                                           seL4_AllRights,
                                           1, /* Cacheable */
                                           &allocman_dynamic_pool);
        ZF_LOGF_IF(reservation.res == NULL, "Failed to reserve a chunk of memory range");
        
        /* Use the newly reserved space as bookkeeping space for allocman. */
        bootstrap_configure_virtual_pool(init_objects.allocman, 
                                         allocman_dynamic_pool,
                                         CONFIG_LIB_INIT_ALLOCMAN_DYNAMIC_POOL_BYTES,
                                         init_objects.page_dir_cap);
    }

    init_objects.initialized = 1;
    return 0;
}





int init_root_task(void) {
    int error;
    static int run_once = 0;

    if(run_once) {
        ZF_LOGF("This function may only be called once");    
    }
    run_once = 1;

    zf_log_set_tag_prefix("root_task:");

#ifdef CONFIG_DEBUG_BUILD
    seL4_DebugNameThread(seL4_CapInitThreadTCB, "root_task");
#endif

    memset(&init_objects, 0, sizeof(init_objects));

    init_objects.info = platsupport_get_bootinfo();
    ZF_LOGF_IF(init_objects.info == NULL, "Failed to get bootinfo.");

    /* Initialize the simple structure's function pointers,
     * Simple manages our bootinfo struct for us. */
    simple_default_init_bootinfo(&init_objects.simple, init_objects.info);

    /**
     * Remap the root task code and data sections to patch the executable permissions.
     */
#ifdef CONFIG_ARCH_ARM
    extern char __executable_start[];
    extern char _etext[];
    extern char _edata[];
    extern char _end[];

    ZF_LOGD("Remapping root task image... start:%p, etext:%p, edata:%p, end:%p",
            __executable_start,
            _etext,
            _edata,
            _end);

    int num_image_caps = simple_get_userimage_count(&init_objects.simple);
    seL4_CPtr page_dir = simple_get_init_cap(&init_objects.simple, seL4_CapInitThreadVSpace);

    /**
     * We have to assume here that the image is contiguous in both physical and 
     * virtual memory. This is pretty brittle, but the only way at the moment.
     */
    void* phys_start = seL4_ARM_Page_GetAddress(simple_get_nth_userimage(&init_objects.simple,
                                                                         0)).paddr;
    ptrdiff_t offset = (uintptr_t)__executable_start - (uintptr_t)phys_start;

    for(int i = 0; i < num_image_caps; i++) {
        seL4_CPtr image_frame = simple_get_nth_userimage(&init_objects.simple, i);
        void *paddr = seL4_ARM_Page_GetAddress(image_frame).paddr;
        void *vaddr = (void*)((uintptr_t)paddr + offset);

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
                return -1;
            }

        }
    }
#endif

    
    /* Setup allocman with a static pool to bootstrap its bookkeeping.
     * Since we are providing it our simple struct it will fill itself
     * with untyped memory chunks from the bootinfo.
     */
    init_objects.allocman = bootstrap_use_current_simple(
                                            &init_objects.simple, 
                                            CONFIG_LIB_INIT_ALLOCMAN_STATIC_POOL_BYTES,
                                            allocman_static_pool);
    ZF_LOGF_IF(init_objects.allocman == NULL, "Failed to bootstrap allocman.");

    /* Initialize the vka object's function pointers.
     * The vka is now backed by the untyped memory in allocman. */
    allocman_make_vka(&init_objects.vka, init_objects.allocman);

    /* 
     * Setup the vspace object. This bookkeeps/manages the virtual memory mappings.
     */
    error = sel4utils_bootstrap_vspace_with_bootinfo_leaky(&init_objects.vspace, 
                                                           &vspace_bootstrap_data,
                                                           simple_get_pd(&init_objects.simple),
                                                           &init_objects.vka,
                                                           init_objects.info);
    ZF_LOGF_IF(error, "Failed to bootstrap vspace");

    /**
     * Setup malloc to refill from our new allocators.
     * Malloc won't work before this point so we have to use reserve_range_no_alloc
     */
    error = sel4utils_reserve_range_no_alloc(&init_objects.vspace,
                                             &heap_res,
                                             CONFIG_LIB_INIT_ROOT_TASK_HEAP_SPACE,
                                             seL4_ReadWrite,
                                             1, /* Cacheable */
                                             &muslc_brk_reservation_start);
    if(error) {
        ZF_LOGE("Failed to reserve range for heap");
        return -3;
    }
    muslc_brk_reservation.res = &heap_res;
    muslc_this_vspace = &init_objects.vspace;
    muslc_vspace_root_cap = simple_get_init_cap(&init_objects.simple, seL4_CapInitThreadVSpace);

    /* Malloc is available now */


    /* At this point all the objects are initialized, but we want to give 
     * allocman more memory for bookkeeping, so we will dynamically allocate
     * some memory for it.
     */

    reservation_t reservation;

    /* Dynamically allocate a new big array/pool of memory using vspace (backed by vka). */
    reservation = vspace_reserve_range(&init_objects.vspace,
                                       CONFIG_LIB_INIT_ALLOCMAN_DYNAMIC_POOL_BYTES,
                                       seL4_AllRights,
                                       1, /* Cacheable */
                                       &allocman_dynamic_pool);
    ZF_LOGF_IF(reservation.res == NULL, "Failed to allocate a chunk of memory range");
    

    /* Use the newly allocated pool as bookkeeping space for allocman. */
    bootstrap_configure_virtual_pool(init_objects.allocman, 
                                     allocman_dynamic_pool,
                                     CONFIG_LIB_INIT_ALLOCMAN_DYNAMIC_POOL_BYTES,
                                     simple_get_pd(&init_objects.simple));



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


    /* Create the bootinfo abstraction layer */
    init_objects.asid_control_cap = simple_get_init_cap(&init_objects.simple, seL4_CapASIDControl);
    init_objects.asid_pool_cap = simple_get_init_cap(&init_objects.simple, seL4_CapInitThreadASIDPool);
    init_objects.tcb_cap = simple_get_init_cap(&init_objects.simple, seL4_CapInitThreadTCB);
    init_objects.cnode_cap = simple_get_init_cap(&init_objects.simple, seL4_CapInitThreadCNode);
    init_objects.page_dir_cap = simple_get_init_cap(&init_objects.simple, seL4_CapInitThreadVSpace);
    init_objects.fault_cap = seL4_CapNull;

    init_objects.initialized = 1;



    return 0;
}


seL4_CPtr init_lookup_ep(const char * name)
{
    if(!init_objects.initialized || !init_objects.init_data) {
        ZF_LOGE("Invalid usage of init library");
        return seL4_CapNull;
    }

    EndpointData * iter = init_objects.init_data->ep_list_head;
    ZF_LOGD_IF(iter == NULL, "No endpoints in list when looking up.");

    while(iter) {
        if(strcmp(name, iter->name) == 0) return (seL4_CPtr)iter->cap;
        iter = iter->next;
    }
    ZF_LOGD("Unable to locate an ep with the given name");
    return seL4_CapNull;
}
