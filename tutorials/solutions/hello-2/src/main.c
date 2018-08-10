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

/* function to run in the new thread */
void * thread_2(void * cookie) {
    /* TASK 3: print something */
    /* hint: printf() */
    printf("thread 2: hello world\n");
    /* never exit */
    while (1);
}

int main(void) {
    UNUSED int err;
    init_root_task();

    /* TASK 1: create a new thread */
    /* hint 1: thread_handle_create(const thread_attr_t *attrs) 
     *
     * hint 2: you can use the library-defined constant 
     *         thread_attr_t thread_defaults_64KB_stack 
     *         for the attributes for this task
     */
    thread_handle_t * child_thread = NULL;
    child_thread = thread_handle_create(&thread_defaults_64KB_stack);
    ZF_LOGF_IF(child_thread == NULL, "Failed to create thread");

    /* TASK 2: start your new thread */
    /* hint 1: int thread_start(thread_handle_t *handle, void *(*start_routine) (void *), void *arg)
     *
     * hint 2: NULL is sufficient for thread args
     */
    err = thread_start(child_thread, thread_2, NULL);
    ZF_LOGF_IF(err, "Failed to start thread");

    /* we are done, say hello */
    printf("main: hello world\n");

    return 0;
}