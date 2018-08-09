/*
 * Copyright 2017, Data61
 * Commonwealth Scientific and Industrial Research Organisation (CSIRO)
 * ABN 41 687 119 230.
 *
 * This software may be distributed and modified according to the terms of
 * the BSD 2-Clause license. Note that NO WARRANTY is provided.
 * See "LICENSE_BSD2.txt" for details.
 *
 * @TAG(DATA61_BSD)
 */

/*
 * seL4 tutorial part 2: create and run a new thread
 */

#include <stdio.h>

#include <init/init.h>
#include <thread/thread.h>
#include <process/process.h>

/* function to run in the new thread */
void * thread_2(void * cookie) {
    
    /* From tutorial 2: print something*/
    printf("thread 2: hello world\n");
    /* never exit */
    while (1);
}

int main(void) {
    UNUSED int err;
    init_root_task();

    /* From tutorial 2: Create your thread */
    thread_handle_t * child_thread = NULL;
    child_thread = thread_handle_create(&thread_defaults_64KB_stack);
    ZF_LOGF_IF(child_thread == NULL, "Failed to create thread");

    /* From tutorial 2: Start your thread */
    err = thread_start(child_thread, thread_2, NULL);
    ZF_LOGF_IF(err, "Failed to start thread");

    /* we are done, say hello */
    printf("main: hello world\n");

    return 0;
}