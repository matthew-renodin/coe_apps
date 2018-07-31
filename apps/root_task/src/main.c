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
#define RUN_DEMO

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
    UNUSED int error, i;
    UNUSED process_handle_t test_procs[NUM_TEST_PROCS];
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
        test_procs_perms[i] = seL4_CanRead;
    }


    UNUSED process_conn_obj_t *ep;
    UNUSED process_conn_obj_t *notif;
    UNUSED process_conn_obj_t *shmem;

    error = process_create_conn_obj(PROCESS_ENDPOINT, "testep", NULL, &ep);
    ZF_LOGF_IF(error, "Failed to create ep");

    error = process_create_conn_obj(PROCESS_NOTIFICATION, "testnotif", NULL, &notif);
    ZF_LOGF_IF(error, "Failed to create notif");

    error = process_create_conn_obj(PROCESS_SHARED_MEMORY, "testshmem", NULL, &shmem);
    ZF_LOGF_IF(error, "Failed to create shmem");

    for(i = 0; i < NUM_TEST_PROCS; i++) {
        error = process_connect(&test_procs[i],
                                ep,
                                process_rwg,
                                &((process_conn_attr_t){.badge=i}),
                                NULL);
        ZF_LOGF_IF(error, "Failed to connect ep");

        error = process_connect(&test_procs[i],
                                notif,
                                process_rw,
                                NULL,
                                NULL);
        ZF_LOGF_IF(error, "Failed to connect notif");

        error = process_connect(&test_procs[i],
                                shmem,
                                process_rw,
                                NULL,
                                NULL);
        ZF_LOGF_IF(error, "Failed to connect shmem");

        error = process_run(&test_procs[i], 1, (char**)&test_procs[i].name);
        assert(error == 0);
    }
    
    UNUSED int *shmem_addr;
    UNUSED seL4_CPtr notif_cap;

    process_conn_ret_t ret;
    error = process_connect(PROCESS_SELF, notif, process_rw, NULL, &ret);
    ZF_LOGF_IF(error, "Failed to connect shmem to self");
    notif_cap = ret.self_cap;

    error = process_connect(PROCESS_SELF, shmem, process_rw, NULL, &ret);
    ZF_LOGF_IF(error, "Failed to connect shmem to self");
    shmem_addr = (int*)ret.self_shmem_addr;


    while(1) {
        seL4_Wait(notif_cap, NULL);
        if(*shmem_addr == 410) break;
        seL4_Signal(notif_cap);
        seL4_Yield();
    }

    
    for(i = 0; i < NUM_TEST_PROCS; i++) {
        error = process_destroy(&test_procs[i]);
        ZF_LOGF_IF(error, "Failed to destroy process");
    }
    
    error = process_free_conn_obj(&ep);
    ZF_LOGF_IF(error, "Failed to free ep");

    error = process_free_conn_obj(&notif);
    ZF_LOGF_IF(error, "Failed to free notif");

    error = process_free_conn_obj(&shmem);
    ZF_LOGF_IF(error, "Failed to free shmem");


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
    process_conn_obj_t *echo1ep;
    err = process_create_conn_obj(PROCESS_ENDPOINT, "echo1-ep", NULL, &echo1ep); 
    ZF_LOGF_IF(err, "Failed to create ep");

    err = process_connect(&child1, echo1ep, process_rwg, NULL, NULL);
    ZF_LOGF_IF(err, "Failed to connect ep");

    err = process_connect(&child2, echo1ep, process_rwg, NULL, NULL);
    ZF_LOGF_IF(err, "Failed to connect ep");


    /**
     * Also give the new processes two pages of shared memory.
     * Each page will be writable by only one process.
     */
    process_conn_obj_t *echo1shmem;
    process_conn_obj_t *echo2shmem;

    err = process_create_conn_obj(PROCESS_SHARED_MEMORY, "echo1-shmem", NULL, &echo1shmem);
    ZF_LOGF_IF(err, "Failed to create shared memory");

    err = process_create_conn_obj(PROCESS_SHARED_MEMORY, "echo2-shmem", NULL, &echo2shmem);
    ZF_LOGF_IF(err, "Failed to create shared memory");

    err = process_connect(&child1, echo1shmem, process_rw, NULL, NULL);
    ZF_LOGF_IF(err, "Failed to connect shared memory");

    err = process_connect(&child2, echo1shmem, process_ro, NULL, NULL);
    ZF_LOGF_IF(err, "Failed to connect shared memory");

    err = process_connect(&child1, echo2shmem, process_ro, NULL, NULL);
    ZF_LOGF_IF(err, "Failed to connect shared memory");

    err = process_connect(&child2, echo2shmem, process_rw, NULL, NULL);
    ZF_LOGF_IF(err, "Failed to connect shared memory");

    /**
     * To synchronize writes/reads to the shared memory use two notification eps.
     */
    process_conn_obj_t *echo1notif;
    process_conn_obj_t *echo2notif;

    err = process_create_conn_obj(PROCESS_NOTIFICATION, "echo1-notif", NULL, &echo1notif);
    ZF_LOGF_IF(err, "Failed to create notification ep");

    err = process_create_conn_obj(PROCESS_NOTIFICATION, "echo2-notif", NULL, &echo2notif);
    ZF_LOGF_IF(err, "Failed to create notification ep");

    err = process_connect(&child1, echo1notif, process_rw, NULL, NULL);
    ZF_LOGF_IF(err, "Failed to connect notification ep");

    err = process_connect(&child2, echo1notif, process_ro, NULL, NULL);
    ZF_LOGF_IF(err, "Failed to connect notification ep");

    err = process_connect(&child1, echo2notif, process_ro, NULL, NULL);
    ZF_LOGF_IF(err, "Failed to connect notification ep");

    err = process_connect(&child2, echo2notif, process_rw, NULL, NULL);
    ZF_LOGF_IF(err, "Failed to connect notification ep");


    /**
     * Give child 1 an ep to send messages to us, the parent.
     */
    process_conn_obj_t *child1_obj;
    err = process_create_conn_obj(PROCESS_ENDPOINT, "parent", NULL, &child1_obj);
    ZF_LOGF_IF(err, "Failed to create ep.");
    
    err = process_connect(&child1, child1_obj, process_rw, NULL, NULL);
    ZF_LOGF_IF(err, "Failed to connect ep.");
    
    process_conn_ret_t ret;
    err = process_connect(PROCESS_SELF, child1_obj, process_rw, NULL, &ret);
    ZF_LOGF_IF(err, "Failed to connect self ep.");

    seL4_CPtr child1_ep = ret.self_cap;


    /**
     * Give child 2 a notification and shared memory to write messages to us, the parent
     */
    process_conn_obj_t *child2_notif;
    err = process_create_conn_obj(PROCESS_NOTIFICATION, "parent", NULL, &child2_notif);
    ZF_LOGF_IF(err, "Failed to create notification.");
    
    err = process_connect(&child2, child2_notif, process_rw, NULL, NULL);
    ZF_LOGF_IF(err, "Failed to connect ep.");
    
    err = process_connect(PROCESS_SELF, child2_notif, process_rw, NULL, &ret);
    ZF_LOGF_IF(err, "Failed to connect self ep.");

    seL4_CPtr child2_ep = ret.self_cap;


    process_conn_obj_t *child2_shmem_obj;
    err = process_create_conn_obj(PROCESS_SHARED_MEMORY, "parent", NULL, &child2_shmem_obj);
    ZF_LOGF_IF(err, "Failed to create notification.");
    
    err = process_connect(&child2, child2_shmem_obj, process_rw, NULL, NULL);
    ZF_LOGF_IF(err, "Failed to connect ep.");
    
    err = process_connect(PROCESS_SELF, child2_shmem_obj, process_rw, NULL, &ret);
    ZF_LOGF_IF(err, "Failed to connect self ep.");

    void *child2_shmem = ret.self_shmem_addr;

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
    ZF_LOGI("\n\nFINISHED ALL TESTS\n\n");
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

