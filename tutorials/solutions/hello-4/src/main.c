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
 * seL4 tutorial part 4: create a new process and IPC with it
 */


/* Include Kconfig variables. */
#include <stdio.h>

#include <init/init.h>
#include <thread/thread.h>
#include <process/process.h>

/* constants */
#define EP_BADGE 0x61 // arbitrary (but unique) number for a badge
#define MSG_DATA 0x6161 // arbitrary data to send

#define APP_PRIORITY seL4_MaxPrio
#define APP_IMAGE_NAME "hello-4-app"

int main(void) {
    init_root_task();
    UNUSED int error = 0;

    /* TASK 1: Create a new process */
    /* hint 1: int process_create(const char *elf_file_name,
     *                            const char *proc_name,
     *                            const process_attr_t *attr,
     *                            process_handle_t *handle);
     * hint 2: the global constant
     *         process_attr_t process_default_attrs
     *         may be helpful here
    */
    process_handle_t child_process;
    error =  process_create(APP_IMAGE_NAME,
                            APP_IMAGE_NAME,
                            &process_default_attrs,
                            &child_process);
    ZF_LOGF_IF(error, "Failed to create child process");
    
    /* TASK 2: Create an endpoint connection */
    /*
     * hint 1: int process_create_conn_obj(process_conn_type_t typ,
     *                                     const char *name,
     *                                     const process_conn_obj_attr_t *attr,
     *                                     process_conn_obj_t **obj);
     * 
     * hint 2: The process_conn_type for endpoints is PROCESS_ENDPOINT
     * 
     * hint 3: process_create_conn_obj will use defaults if argument 3 is NULL
     */
    process_conn_obj_t *endpoint;
    error = process_create_conn_obj(PROCESS_ENDPOINT,
                                  "Parent-Child",
                                  NULL,
                                  &endpoint);
    ZF_LOGF_IF(error, "Failed to create connection object");

    /* TASK 3: Connect the root_task thread to the connection */
    /*
     * hint 1: int process_connect(process_handle_t *handle,
     *                             process_conn_obj_t *obj,
     *                             process_conn_perms_t perms,
     *                             process_conn_attr_t *attr,
     *                             process_conn_ret_t *ret);
     * 
     * hint 2: PROCESS_SELF is used in place of handle for connecting self
     * 
     * hint 2: process_rwg is acceptable for perms
     * 
     * hint 3: NULL will use defaults for attr
     * 
     * hint 4: ret is an out parameter when connecting self, so set that
     */
    process_conn_ret_t connection;
    error = process_connect(PROCESS_SELF, endpoint, process_rwg, NULL, &connection);
    ZF_LOGF_IF(error, "Failed to connect self to endpoint");

    /* TASK 2: Initialize the child process's connection with a badged ep */
    process_conn_attr_t badged_attr = {
        .badge = EP_BADGE
    };

    /* TASK 3: Connect your child process to the connection */
    /*
     * hint 1: int process_connect(process_handle_t *handle,
     *                             process_conn_obj_t *obj,
     *                             process_conn_perms_t perms,
     *                             process_conn_attr_t *attr,
     *                             process_conn_ret_t *ret);
     * 
     * hint 2: process_rwg is acceptable for perms
     * 
     * hint 3: ret is only used when connecting to self, so that can be NULL
     */
    error = process_connect(&child_process, endpoint, process_rwg, &badged_attr, NULL);
    ZF_LOGF_IF(error, "Failed to connect child to endpoint");

    /* TASK 3: Start the process */
    /* hint 1: int process_run(process_handle_t *handle, int argc, char *argv[]);
     * hint 2: In this case, set argv[0] to the name of the connection that the process will use
     */
    char *argv[] = { "Parent-Child" }; 
    error = process_run(&child_process, 1, argv);

    /* we are done, say hello */
    printf("main: hello world\n");

    /*
     * now wait for a message from the new process, then send a reply back
     */

    seL4_Word sender_badge = 0;
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0, 0);
    seL4_Word msg;

    /* TASK 4: wait for a message */
    /* hint 1: seL4_Recv()
     * seL4_MessageInfo_t seL4_Recv(seL4_CPtr src, seL4_Word* sender)
     * @param src The capability to be invoked.
     * @param sender The badge of the endpoint capability that was invoked by the sender is written to this address.
     * @return A seL4_MessageInfo_t structure
     * Link to source: https://wiki.sel4.systems/seL4%20Tutorial%204#TASK_7:
     * You can find out more about it in the API manual: http://sel4.systems/Info/Docs/seL4-manual-3.0.0.pdf
     *
     * hint 2: seL4_MessageInfo_t is generated during build.
     * The type definition and generated field access functions are defined in a generated file:
     * build/x86/pc99/libsel4/include/sel4/types_gen.h
     * It is generated from the following definition:
     * Link to source: https://wiki.sel4.systems/seL4%20Tutorial%204#TASK_7:
     * You can find out more about it in the API manual: http://sel4.systems/Info/Docs/seL4-manual-3.0.0.pdf
     *
     * hint 3: use the badged endpoint cap that you minted above
     */

    tag = seL4_Recv(connection.self_cap, &sender_badge);

    /* make sure it is what we expected */
    ZF_LOGF_IF(sender_badge != EP_BADGE,
               "The badge we received from the new thread didn't match our expectation.\n");

    ZF_LOGF_IF(seL4_MessageInfo_get_length(tag) != 1,
               "Response data from the new process was not the length expected.\n"
               "\tHow many registers did you set with seL4_SetMR within the new process?\n");


    /* get the message stored in the first message register */
    msg = seL4_GetMR(0);
    printf("main: got a message %#x from %#x\n", msg, sender_badge);

    /* modify the message */
    seL4_SetMR(0, ~msg);

    /* TASK 5: send the modified message back */
    /* hint 1: seL4_ReplyRecv()
     * seL4_MessageInfo_t seL4_ReplyRecv(seL4_CPtr dest, seL4_MessageInfo_t msgInfo, seL4_Word *sender)
     * @param dest The capability to be invoked.
     * @param msgInfo The messageinfo structure for the IPC.  This specifies information about the message to send (such as the number of message registers to send) as the Reply part.
     * @param sender The badge of the endpoint capability that was invoked by the sender is written to this address. This is a result of the Wait part.
     * @return A seL4_MessageInfo_t structure.  This is a result of the Wait part.
     * Link to source: https://wiki.sel4.systems/seL4%20Tutorial%204#TASK_8:
     * You can find out more about it in the API manual: http://sel4.systems/Info/Docs/seL4-manual-3.0.0.pdf
     *
     * hint 2: seL4_MessageInfo_t is generated during build.
     * The type definition and generated field access functions are defined in a generated file:
     * build/x86/pc99/libsel4/include/sel4/types_gen.h
     * It is generated from the following definition:
     * Link to source: https://wiki.sel4.systems/seL4%20Tutorial%204#TASK_8:
     * You can find out more about it in the API manual: http://sel4.systems/Info/Docs/seL4-manual-3.0.0.pdf
     *
     * hint 3: use the endpoint cap that you used for Call
     */
    seL4_ReplyRecv(connection.self_cap, tag, &sender_badge);


    return 0;
}