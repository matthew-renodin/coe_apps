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
 * seL4 tutorial part 4: application to be run in a process
 */

#include <stdio.h>

#include <init/init.h>

/* constants */
#define MSG_DATA 0x6161 //  arbitrary data to send

int main(int argc, char **argv) {
    /* TASK 6: Initialize the child process */
    /* hint 1: init_process();
     * This is similar to init_root_task, but it sets things up a little
     * differently because resources are provided from the parent intstead
     * of from the kernel itself
     */
    init_process();

    seL4_MessageInfo_t tag;
    seL4_Word msg;

    printf("process_2: hey hey hey\n");

    /*
     * send a message to our parent, and wait for a reply
     */

    /* set the data to send. We send it in the first message register */
    tag = seL4_MessageInfo_new(0, 0, 0, 1);
    seL4_SetMR(0, MSG_DATA);

    /* TASK 7: send and wait for a reply */
    /* hint 1: seL4_Call()
     * seL4_MessageInfo_t seL4_Call(seL4_CPtr dest, seL4_MessageInfo_t msgInfo)
     * @param dest The capability to be invoked.
     * @param msgInfo The messageinfo structure for the IPC.  This specifies information about the message to send (such as the number of message registers to send).
     * @return A seL4_MessageInfo_t structure.  This is information about the repy message.
     * Link to source: https://wiki.sel4.systems/seL4%20Tutorial%204#TASK_9:
     * You can find out more about it in the API manual: http://sel4.systems/Info/Docs/seL4-manual-3.0.0.pdf
     *
     * hint 2: send the endpoint cap using argv (see TASK 3 in the other main.c)
     */
    ZF_LOGF_IF(argc < 1,
               "Missing arguments.\n");
    seL4_CPtr ep = init_lookup_endpoint(argv[0]);

    tag = seL4_Call(ep, tag);

    
    /* check that we got the expected reply */
    ZF_LOGF_IF(seL4_MessageInfo_get_length(tag) != 1,
               "Length of the data send from root thread was not what was expected.\n"
               "\tHow many registers did you set with seL4_SetMR, within the root thread?\n");

    msg = seL4_GetMR(0);
    ZF_LOGF_IF(msg != ~MSG_DATA,
               "Unexpected response from root thread.\n");

    printf("process_2: got a reply: %#x\n", msg);

    return 0;
}