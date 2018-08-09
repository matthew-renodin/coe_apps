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
 * seL4 tutorial part 1:  simple printf
 */

#include <init/init.h>

#include <stdio.h>

/* TASK 0: Create the main function */
int main(void)
{
    /* TASK 1: initialize the root task */
    /* hint: init_root_task() */
    init_root_task();

    /* TASK 2: initialize the root task */
    /* hint: printf() */
    printf("Hello World!\n");
    return 0;
}