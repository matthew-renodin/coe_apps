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
 * @file main.c
 * @brief Implementation of a demo root task on top of seL4 
 *
 */

#define _GNU_SOURCE
/* Include Kconfig variables. */
#include <autoconf.h>

/* Include libc headers */
#include <stdlib.h>
#include <stdio.h>
#include <time.h>

/* Include seL4 Libraries */
#include <sel4/sel4.h>
#include <utils/util.h>
#include <platsupport/plat/serial.h>

/* Include seL4 COE library headers */
#include <init/init.h>
#include <process/process.h>

#define RUN_TESTS
//#define RUN_DEMO

#define NUM_TEST_PROCS 5


UNUSED uintptr_t expected_cookie = 0xdeadbeef;
UNUSED uintptr_t expected_return = 0xfeebdaed;
UNUSED thread_handle_t *test_helper_handle;


UNUSED static void *test_helper_func(void *cookie) {
    
    assert((uintptr_t)cookie == expected_cookie);

    thread_handle_t *handle = thread_handle_get_current();
    assert(handle == test_helper_handle);

    return (void*)expected_return;
}


UNUSED static void test_libthread(void) {
    int error;

    ZF_LOGD("Starting libthread test.");

    /**
     * Test the default attribute values
     */
    assert(thread_1mb_high_priority.stack_size_pages == 256);

    /**
     * Testing handle creation
     */
    test_helper_handle = thread_handle_create(NULL);
    assert(test_helper_handle == NULL);

    test_helper_handle = thread_handle_create(&thread_1mb_high_priority);
    assert(test_helper_handle != NULL);

    /**
     * Testing thread starting and joining
     */
    error = thread_start(NULL, test_helper_func, (void *)expected_cookie);

    error = thread_start(test_helper_handle, test_helper_func, (void *)expected_cookie);
    assert(error == 0);

    error = thread_start(test_helper_handle, test_helper_func, (void *)expected_cookie);
    assert(error != 0);

    void *ret = thread_join(test_helper_handle);
    assert(ret == (void*)expected_return);
    
    ret = thread_join(test_helper_handle);
    assert(ret == (void*)expected_return);

    error = thread_start(test_helper_handle, test_helper_func, (void *)expected_cookie);
    assert(error != 0);

    /**
     * Testing thread destruction
     */
    error = thread_destroy_free_handle(&test_helper_handle);
    assert(error == 0);
    assert(test_helper_handle == NULL);

    error = thread_destroy_free_handle(&test_helper_handle);
    assert(error != 0);

    ZF_LOGD("Finished libthread test.");
}


UNUSED static void test_libprocess(void) {
    int error, i;
    process_handle_t test_procs[NUM_TEST_PROCS];
    process_handle_t *test_procs_refs[NUM_TEST_PROCS];
    process_handle_t *test_procs_bad_refs[NUM_TEST_PROCS + 1];
    UNUSED seL4_CapRights_t test_procs_perms[NUM_TEST_PROCS];
    
    ZF_LOGD("Starting libprocess test.");

    error = process_create(NULL,
                           "test_child",
                           &process_default_attrs,
                           &test_procs[0]);
    assert(error != 0);

    error = process_create("",
                           "test_child",
                           &process_default_attrs,
                           &test_procs[0]);
    assert(error != 0);


    for(i = 0; i < NUM_TEST_PROCS; i++) {
        char *proc_name;
        error = asprintf(&proc_name, "test_proc%i", i);
        assert(error > 0);

        error = process_create("test_proc",
                               proc_name,
                               &process_default_attrs,
                               &test_procs[i]);
        assert(error == 0);
        test_procs_refs[i] = &test_procs[i];
        test_procs_bad_refs[i] = &test_procs[i];
        test_procs_perms[i] = seL4_CanRead;
    }

    test_procs_bad_refs[NUM_TEST_PROCS] = NULL;

    void *shmem_addr;

    error = process_connect_many_to_self_shmem(NULL,
                                               test_procs_perms,
                                               NUM_TEST_PROCS,
                                               4,
                                               "shmem-many-self-test-1",
                                               &shmem_addr);
    assert(error != 0);

    error = process_connect_many_to_self_shmem(test_procs_refs,
                                               NULL,
                                               NUM_TEST_PROCS,
                                               4,
                                               "shmem-many-self-test-2",
                                               &shmem_addr);
    assert(error != 0);

    error = process_connect_many_to_self_shmem(test_procs_refs,
                                               test_procs_perms,
                                               0,
                                               4,
                                               "shmem-many-self-test-3",
                                               &shmem_addr);
    assert(error == 0);
    /* TODO check and make sure that nothing was allocated */

    error = process_connect_many_to_self_shmem(test_procs_refs,
                                               test_procs_perms,
                                               NUM_TEST_PROCS,
                                               0,
                                               "shmem-many-self-test-4",
                                               &shmem_addr);
    assert(error == 0);
    /* TODO check and make sure that nothing was allocated */

    error = process_connect_many_to_self_shmem(test_procs_refs,
                                               test_procs_perms,
                                               NUM_TEST_PROCS,
                                               0,
                                               NULL,
                                               &shmem_addr);
    assert(error != 0);


    error = process_connect_many_to_self_shmem(test_procs_bad_refs,
                                               test_procs_perms,
                                               NUM_TEST_PROCS,
                                               4,
                                               "shmem-many-self-test",
                                               &shmem_addr);
    assert(error == 0);




    ZF_LOGD("Finished libprocess test.");
}


UNUSED static void test_process_leaks(void) {
    int err;
    uint64_t num_cycles = 0;

    /**
     * Test process destruction for leaks.
     */
    while(1) {
        num_cycles++;
        process_handle_t dummy;
        err = process_create("dummy", /* File name */
                             "dummy",        /* Process name */
                             &process_default_attrs,
                             &dummy);
        ZF_LOGF_IF(err, "Failed to create dummy. cycles: %lu", num_cycles);

        char *argv[] = { "\0" };
        err = process_run(&dummy, sizeof(argv)/sizeof(argv[0]), argv);
        ZF_LOGF_IF(err, "Failed to run dummy. cycles: %lu", num_cycles);

        seL4_Yield();
        
        err = process_destroy(&dummy);
        ZF_LOGF_IF(err, "Failed to destroy dummy. cycles: %lu", num_cycles);
    }

}


UNUSED static void *vka_abuser(void* arg) {
    while(1) {
        vka_object_t ob;
        int error = vka_alloc_endpoint(&init_objects.vka, &ob);
        ZF_LOGF_IF(error, "Failed to alloc ep");
        vka_free_object(&init_objects.vka, &ob);
    }
    return NULL;
}

UNUSED static void *vspace_abuser(void* arg) {
    while(1) {
        void *addr;
        seL4_Word num_pages = 1;
        reservation_t res = vspace_reserve_range(&init_objects.vspace,
                                                 num_pages * PAGE_SIZE_4K,
                                                 seL4_AllRights,
                                                 1,
                                                 &addr);
        ZF_LOGF_IF(res.res == NULL, "Failed to reserve range");
        int error = vspace_new_pages_at_vaddr(&init_objects.vspace,
                                              addr,
                                              num_pages,
                                              PAGE_BITS_4K,
                                              res);
        ZF_LOGF_IF(error, "Failed to make new pages");
        vspace_unmap_pages(&init_objects.vspace,
                           addr,
                           num_pages,
                           PAGE_BITS_4K,
                           &init_objects.vka);
        vspace_free_reservation(&init_objects.vspace, res);
    }
    return NULL;
}

UNUSED static void test_thread_init_objects(void) {
    int err;
    /**
     * Try to crash: test vka, vspace locking.
     */
    for(int i = 0; i < 4; i++) {
        thread_attr_t ts_test_attr = thread_1mb_high_priority;
        ts_test_attr.cpu_affinity = i % 4;
        thread_handle_t *thread_safe_tester = thread_handle_create(&ts_test_attr);
        ZF_LOGF_IF(thread_safe_tester == NULL, "Failed to create thread");

        err = thread_start(thread_safe_tester, vspace_abuser, NULL);
        ZF_LOGF_IF(err, "Failed to create thread");
    }

}



UNUSED static void demo(void) {
    int err;
    process_handle_t child1, child2;

    /**
     * Create two new processes
     */
    err = process_create("child_example", /* File name */
                         "child1",        /* Process name */
                         &process_default_attrs,
                         &child1);
    ZF_LOGF_IF(err, "Failed to create child1");

    err = process_create("child_example", /* File name */
                         "child2",        /* Process name */
                         NULL,
                         &child2);
    ZF_LOGF_IF(err, "Failed to create child2");

    /**
     * Give the new processes an IPC endpoint to communicate
     */
    err = process_connect_pair_to_endpoint(&child1, seL4_AllRights,
                                           &child2, seL4_AllRights,
                                           "echo1-ep");
    ZF_LOGF_IF(err, "Failed to create ep");


    /**
     * Also give the new processes two pages of shared memory.
     * Each page will be writable by only one process.
     */
    err = process_connect_pair_to_shmem(&child1, seL4_ReadWrite,
                                        &child2, seL4_CanRead,
                                        1, /* Number of pages */
                                        "echo1-shmem");   
    ZF_LOGF_IF(err, "Failed to create shared memory");

    err = process_connect_pair_to_shmem(&child1, seL4_CanRead,
                                        &child2, seL4_ReadWrite,
                                        1, /* Number of pages */
                                        "echo2-shmem"); 
    ZF_LOGF_IF(err, "Failed to create shared memory");


    /**
     * To synchronize writes/reads to the shared memory use two notification eps.
     */
    err = process_connect_pair_to_notification(&child1, seL4_ReadWrite,
                                               &child2, seL4_CanRead,
                                               "echo1-notif"); 
    ZF_LOGF_IF(err, "Failed to create notification ep");

    err = process_connect_pair_to_notification(&child1, seL4_CanRead,
                                               &child2, seL4_ReadWrite,
                                               "echo2-notif");
    ZF_LOGF_IF(err, "Failed to create notification ep");


    /**
     * Give child 1 an ep to send messages to us, the parent.
     */
    seL4_CPtr child1_ep;
    err = process_connect_to_self_endpoint(&child1, seL4_ReadWrite, "parent", &child1_ep);
    ZF_LOGF_IF(err, "Failed to create self ep.");


    /**
     * Give child 2 a notification and shared memory to write messages to us, the parent
     */
    seL4_CPtr child2_ep;
    err = process_connect_to_self_notification(&child2, seL4_ReadWrite, "parent", &child2_ep);
    ZF_LOGF_IF(err, "Failed to create self notification.");

    void *child2_shmem;
    err = process_connect_to_self_shmem(&child2, seL4_ReadWrite, 1, "parent", &child2_shmem);
    ZF_LOGF_IF(err, "Failed to create self notification.");


    /**
     * Give each process 16 MB (2^20*16) of untyped kernel objects
     */
    err = process_give_untyped_resources(&child1, 20, 16);
    ZF_LOGF_IF(err, "Failed to give untyped.");

    err = process_give_untyped_resources(&child2, 20, 16);
    ZF_LOGF_IF(err, "Failed to give untyped.");



#ifdef CONFIG_PLAT_ZYNQMP
    err = process_map_device_pages_give_caps(&child1,
                                             (void *)UART1_PADDR,
                                             1, /* # of pages */
                                             PAGE_BITS_4K,
                                             "UART1-dma");
    ZF_LOGF_IF(err, "Failed to map UART device");
    
    err = process_add_device_irq(&child1, UART1_IRQ, "UART1-irq");
    ZF_LOGF_IF(err, "Failed to give IRQ device");
#endif

    char *argv1[] = { "child1", "echo1-ep" }; 
    char *argv2[] = { "child2", "echo1-ep" };
    err = process_run(&child1, sizeof(argv1)/sizeof(argv1[0]), argv1);
    err = process_run(&child2, sizeof(argv2)/sizeof(argv2[0]), argv2);

    seL4_MessageInfo_t msg = seL4_Recv(child1_ep, NULL);
    printf("Recieved msg from child 1: %lu\n", (long unsigned)seL4_MessageInfo_get_label(msg));

    seL4_Wait(child2_ep, NULL); 
    printf("Recieved msg from child 2: %s\n", (const char *)child2_shmem);

    /**
     * Test process destruction.
     */
    ZF_LOGD("Destroying 1...");
    process_destroy(&child1);
    seL4_DebugDumpScheduler();

    ZF_LOGD("Destroying 2...");
    process_destroy(&child2);
    seL4_DebugDumpScheduler();


    seL4_DebugProcMap();
}



/**
 * Demo entry point after kernel boots.
 */
int main(void) {
    int err;

    err = init_root_task();
    ZF_LOGF_IF(err, "Failed to init");

#ifdef RUN_TESTS
    test_libthread();
    test_libprocess();
    //test_thread_init_objects();
    //test_process_leaks();
#endif

#ifdef RUN_DEMO
    demo();
#endif

    return 0;
}


/**
 * Avoid main falling off the end of the world.
 */
void abort(void) {
    while(1) { 
        nanosleep(&(struct timespec){.tv_sec=1, .tv_nsec=0}, NULL);
    }
}

