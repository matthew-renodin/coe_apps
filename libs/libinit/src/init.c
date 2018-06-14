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

#include <sel4/sel4.h>
#include <vka/vka.h>
#include <vspace/vspace.h>
#include <simple/simple.h>
#include <simple-default/simple-default.h>
#include <allocman/allocman.h>
#include <allocman/vka.h>
#include <allocman/bootstrap.h>
#include <sel4platsupport/bootinfo.h>
#include <utils/zf_log.h>
#include <sel4utils/sel4_zf_logif.h>
#include <sel4utils/vspace.h>

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
static vspace_t vspace;
static vka_t vka;
static allocman_t *allocman;
static simple_t simple;
static seL4_BootInfo *info;


/* In order for allocman to start bookkeeping it needs memory. 
 * This array is memory for it to bootstrap itself before untyped 
 * memory is accessable. After that it can allocate its own pages,
 * with a limit set by the dynamic pool size.
 */
static uint8_t allocman_static_pool[CONFIG_LIB_INIT_ALLOCMAN_STATIC_POOL_BYTES];
static void *  allocman_dynamic_pool;

/* Static memory to help bootstrap the virtual memory bookkeeping */
static sel4utils_alloc_data_t vspace_bootstrap_data;

static void print_coe_banner(void) {
    printf(
            "   __________  ____   _____     ____\n"
            "  / __/ __/ / / / /  / ___/__  / __/\n"
            " _\\ \\/ _// /_/_  _/ / /__/ _ \\/ _/  \n"
            "/___/___/____//_/   \\___/\\___/___/  \n"
            "                                    \n");
    printf("Booting up...\n\n\n");  
}


int init(void) {
    /* TODO: In progress */
    return 0;
}

int init_root_task(void) {
    int error;
    static int run_once = 0;

    if(run_once) return -1;
    
    zf_log_set_tag_prefix("root_task:");

    print_coe_banner();

    info = platsupport_get_bootinfo();
    ZF_LOGF_IF(info == NULL, "Failed to get bootinfo.");


    /* Initialize the simple structure's function pointers,
     * Simple manages our bootinfo struct for us. */
    simple_default_init_bootinfo(&simple, info);

#ifdef CONFIG_DEBUG_BUILD
    /* This will print the available untypeds */
    simple_print(&simple);

    seL4_DebugNameThread(seL4_CapInitThreadTCB, "root_task");
#endif

    /* Setup allocman with a static pool to bootstrap its bookkeeping.
     * Since we are providing it our simple struct it will fill itself
     * with untyped memory chunks from the bootinfo.
     */
    allocman = bootstrap_use_current_simple(&simple, 
                                            CONFIG_LIB_INIT_ALLOCMAN_STATIC_POOL_BYTES,
                                            allocman_static_pool);
    ZF_LOGF_IF(allocman == NULL, "Failed to bootstrap allocman.");

    /* Initialize the vka object's function pointers.
     * The vka is now backed by the untyped memory in allocman. */
    allocman_make_vka(&vka, allocman);

    /* Setup the vspace object. This bookkeeps/manages the virtual memory mappings.
     * We don't want vspace to free objects so we use the "leaky" call.
     */
    error = sel4utils_bootstrap_vspace_with_bootinfo_leaky(&vspace, 
                                                   &vspace_bootstrap_data,
                                                   simple_get_pd(&simple),
                                                   &vka,
                                                   info);
    ZF_LOGF_IF(error, "Failed to bootstrap vspace");


    /* At this point all the objects are initialized, but we want to give 
     * allocman more memory for bookkeeping, so we will dynamically allocate
     * some memory for it.
     */

    reservation_t reservation;

    /* Dynamically allocate a new big array/pool of memory using vspace (backed by vka). */
    reservation = vspace_reserve_range(&vspace,
                                       CONFIG_LIB_INIT_ALLOCMAN_DYNAMIC_POOL_BYTES,
                                       seL4_AllRights,
                                       1, /* Cacheable */
                                       &allocman_dynamic_pool);
    ZF_LOGF_IF(reservation.res == NULL, "Failed to allocate a chunk of memory range");
    

    /* Use the newly allocated pool as bookkeeping space for allocman. */
    bootstrap_configure_virtual_pool(allocman, 
                                     allocman_dynamic_pool,
                                     CONFIG_LIB_INIT_ALLOCMAN_DYNAMIC_POOL_BYTES,
                                     simple_get_pd(&simple));

    return 0;
}



