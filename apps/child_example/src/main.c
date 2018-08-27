/*
 * Copyright 2018, Intelligent Automation, Inc.
 * This software was developed in part under Air Force contract number FA8750-15-C-0066 and DARPA 
 *  contract number 140D6318C0001.
 * This software was released under DARPA, public release number 0.6.
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
 * @brief Implementation of a demo child process. 
 *
 */

/* Include Kconfig variables. */
#include <autoconf.h>

/* Include libc headers */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include <sel4/sel4.h>

/* Include seL4 COE library headers */
#include <init/init.h>
#include <thread/thread.h>
#include <process/process.h>


UNUSED char *my_name;
UNUSED char *ep_name;

void * worker_thread(void *cookie) {

    printf("Worker thread id %lu\n", (long unsigned)thread_get_id());

    seL4_CPtr ep_cap = init_lookup_endpoint(ep_name);

    char *shmem[2];
    shmem[0] = init_lookup_shmem("echo1-shmem");
    shmem[1] = init_lookup_shmem("echo2-shmem");

    seL4_CPtr notifs[2];
    notifs[0] = init_lookup_notification("echo1-notif");
    notifs[1] = init_lookup_notification("echo2-notif");

    if(strcmp(my_name, "child1") == 0) {
        seL4_Send(ep_cap, seL4_MessageInfo_new(99,0,0,0));
        strcpy(shmem[0], "Hello  brother #2!\n");
        seL4_Signal(notifs[0]);
        seL4_Wait(notifs[1], NULL);
        printf("Got a message from #2: %s\n", shmem[1]);

        seL4_Send(init_lookup_endpoint("parent"), seL4_MessageInfo_new(66,0,0,0));

    } else {
        seL4_MessageInfo_t msg = seL4_Recv(ep_cap, NULL);
        printf("Got message %lu\n", (long unsigned)seL4_MessageInfo_get_label(msg));

        strcpy(shmem[1], "Hello  brother #1!\n");
        seL4_Signal(notifs[1]);
        seL4_Wait(notifs[0], NULL);
        printf("Got a message from #1: %s\n", shmem[0]);

        strcpy((char *)init_lookup_shmem("parent"), "Hi mom!");
        seL4_Signal(init_lookup_notification("parent"));
    }

    return (void*)42;
}

/**
 * Demo entry point
 */
int main(int argc, char **argv) {
    int error;

    error = init_process();
    ZF_LOGF_IF(error, "Failed to init child process");

    my_name = argv[0];
    ep_name = argv[1];

    process_handle_t child;
    error = process_create("dummy",
                           "grandchild_example",
                           &process_default_attrs,
                           &child);
    ZF_LOGF_IF(error, "Failed to create grandchild");

    char *child_argv[] = { "gather round children" };
    error = process_run(&child, sizeof(child_argv)/sizeof(child_argv[0]), child_argv);



    thread_handle_t *worker = thread_handle_create(&thread_defaults_1MB_stack);
    ZF_LOGF_IF(worker == NULL, "Failed to create thread.");

    error = thread_start(worker, worker_thread, (void*)0xdeadbeef);
    ZF_LOGF_IF(error, "Failed to start thread");


    
    ZF_LOGI("Worker thread result: %lu\n", (long unsigned)thread_join(worker));
    thread_destroy_free_handle(&worker);


    
    return 0;
}



/**
 * Avoid main falling off the end of the world.
 */
void abort(void) {
    while(1) {
        ZF_LOGD("%s still alive.", my_name);
        nanosleep(&(struct timespec){.tv_sec=15, .tv_nsec=0}, NULL);
    }
}

