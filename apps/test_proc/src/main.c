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
 * @file main.c
 * @brief Helper process for testing the COE libs.
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


/**
 * Demo entry point
 */
int main(int argc, char **argv) {
    int error;

    error = init_process();
    ZF_LOGF_IF(error, "Failed to init child process");

    seL4_CPtr testep = init_lookup_endpoint("testep");
    ZF_LOGF_IF(testep == seL4_CapNull, "Failed to lookup testep");

    seL4_Word my_num = (seL4_Word)argv[0][9] - (seL4_Word)'0';
    const seL4_Word offset = 100;

    if(strcmp(argv[0], "test_proc0") == 0) {
        while(1) {
            seL4_Word badge;
            seL4_MessageInfo_t msg = seL4_Recv(testep, &badge);
            seL4_Word num = seL4_MessageInfo_get_label(msg);
            ZF_LOGI("Recieved: %lu from %lu", (long unsigned)num, (long unsigned)badge);
            seL4_Reply(seL4_MessageInfo_new(num + offset,0,0,0));
        }
    } else {
        seL4_MessageInfo_t msg = seL4_Call(testep, seL4_MessageInfo_new(my_num,0,0,0));

        seL4_Word reply = seL4_MessageInfo_get_label(msg);
        
        ZF_LOGI("Got Reply: %lu", (long unsigned)reply);
        ZF_LOGF_IF(reply != my_num + offset, "Invalid reply recieved");

        int *shmem = (int *)init_lookup_shmem("testshmem");
        ZF_LOGF_IF(shmem == NULL, "Failed to lookup testshmem");

        seL4_CPtr notif = init_lookup_notification("testnotif");
        ZF_LOGF_IF(notif == seL4_CapNull, "Failed to lookup testnotif");

        ZF_LOGD("Shmem addr %p", shmem);
        if(strcmp(argv[0], "test_proc1") == 0) {
            *shmem = reply;
            ZF_LOGI("Writing shmem %i", *shmem);
            seL4_Signal(notif);
        } else {
            seL4_Wait(notif, NULL);
            *shmem += reply;
            ZF_LOGI("Writing shmem %i", *shmem);
            seL4_Signal(notif);
        }
    }

    return 0;
}



/**
 * Avoid main falling off the end of the world.
 */
void abort(void) {
    while(1) {
        ZF_LOGD("Test proc still alive.");
        nanosleep(&(struct timespec){.tv_sec=5, .tv_nsec=0}, NULL);
    }
}

