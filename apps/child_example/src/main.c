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

char _cpio_archive[1]; /* TODO remove */


//void * worker_thread(void *cookie) {
//    printf("Worker thread %lu: Made it!\n", thread_get_id());
//
//    
//
//    return NULL;
//}


/**
 * Demo entry point
 */
int main(int argc, char **argv) {
    init_process();

    printf("Looking up ep: %s\n", argv[1]);
    seL4_CPtr ep_cap = init_lookup_ep(argv[1]);
    printf("Found cap in slot: %d\n", (int)ep_cap);

    if(strcmp(argv[0], "child1")) {
        seL4_Send(ep_cap, seL4_MessageInfo_new(99,0,0,0));
    } else {
        seL4_MessageInfo_t msg = seL4_Recv(ep_cap, NULL);
        printf("Got message %lu\n", (long unsigned)seL4_MessageInfo_get_label(msg));
    }
        

    //thread_handle_t worker;
    //thread_handle_create(256, seL4_MaxPrio, 0, &worker);

    //thread_start(&worker, worker_thread, NULL);


    /* TODO demo connection/ep communication */

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

