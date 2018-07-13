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


/**
 * Print a hello world message, character by character. 
 */
UNUSED static void fancy_hello_world() { 
    static const char hello_msg[] = "\n\nI'm sorry, Dave. I'm afraid I can't do that. \n.\n.\n.\n";
    
    for(int i = 0; i < sizeof(hello_msg)/sizeof(hello_msg[0]) - 1; i++) {
        printf("%c", hello_msg[i]);
        fflush(stdout);

        nanosleep(&(struct timespec){.tv_sec=0, .tv_nsec=250*1000*1000}, NULL);
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



/**
 * Demo entry point after kernel boots.
 */
int main(void) {
    int err;
    process_handle_t child1, child2;

    err = init_root_task();
    ZF_LOGF_IF(err, "Failed to init");

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

    /**
     * Test process destruction.
     */
    //process_destroy(&child1);
    //process_destroy(&child2);

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

