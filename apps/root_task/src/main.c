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

/* Include seL4 COE library headers */
#include <init/init.h>
#include <process/process.h>


/**
 * Print a hello world message, character by character. 
 */
static void fancy_hello_world() { 
    static const char hello_msg[] = "\n\nI'm sorry, Dave. I'm afraid I can't do that. \n.\n.\n.\n";
    
    for(int i = 0; i < sizeof(hello_msg)/sizeof(hello_msg[0]) - 1; i++) {
        printf("%c", hello_msg[i]);
        fflush(stdout);

        nanosleep(&(struct timespec){.tv_sec=0, .tv_nsec=250*1000*1000}, NULL);
    }
}


/**
 * Demo entry point after kernel boots.
 */
int main(void) {
    init_root_task();

    fancy_hello_world();

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

