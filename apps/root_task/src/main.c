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

