#include <autoconf.h>

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <stdint.h>
#include <unistd.h>
#include <sched.h>
#include <time.h>

#include <sel4/sel4.h>
#include <sel4debug/debug.h>

#include <utils/arith.h>
#include <utils/zf_log.h>
#include <sel4utils/sel4_zf_logif.h>

/* global environment variables */
seL4_BootInfo *info;

#define NUM_CNODE_SLOTS_BITS 4

/* stack for the new thread */
#define NUM_WORKER_THREADS 10
#define WORKER_THREAD_STACK_SIZE 4096
static uint64_t worker_thread_stacks[NUM_WORKER_THREADS][WORKER_THREAD_STACK_SIZE] __attribute__ ((aligned (16)));

#define MAX_NAME_LENGTH 256
static char worker_thread_names[NUM_WORKER_THREADS][MAX_NAME_LENGTH];

/* convenience function */
extern void name_thread(seL4_CPtr tcb, char *name);

/* returns a cap to an untyped with a size at least size_bytes, or -1 if none exists */
seL4_CPtr get_untyped(seL4_BootInfo *info, int size_bytes) {

    for (int i = info->untyped.start, idx = 0; i < info->untyped.end; ++i, ++idx) {
        if (BIT(info->untypedList[idx].sizeBits) >= size_bytes) {
            return i;
        }
    }

    return -1;
}

static seL4_CPtr ep_cap_start;

static int stop_test = 0;

/*
void *safe_malloc(int size) {
    static int safe_lock = 0;
    void *vp = NULL;
    int spincount = 0;

    while( ! __sync_bool_compare_and_swap(&safe_lock, 0, 1)){
        seL4_Yield();
        spincount++;
        if(spincount > 10)seL4_Sleep(10);
    }
    vp = malloc(size);

    safe_lock = 0;

    return vp;
    
}
*/

/* function to run in the new thread */
void worker_thread(void) {
    int* buf;
    printf("Worker thread: hallo wereld. stack %p\n", &buf);
    
    while (!stop_test) {
        seL4_MessageInfo_t msg;
        seL4_Word badge;
        msg = seL4_Recv(ep_cap_start, &badge);
        seL4_Send(ep_cap_start, msg);
        //buf = (int *)malloc(16);
        //if(buf == NULL) {
        //    printf("\n\nWorker thread: Allocation failed!\n\n");
        //    stop_test = 1;
        //    break;
        //}
        //*buf = 2;
        ////struct timespec r = {.tv_sec = 1, .tv_nsec = 0};
        ////nanosleep(&r, NULL);
        ////sched_yield();
        ////printf("Worker %p: allocated %p\n", &buf, buf);
        //if(*buf != 2) {
        //    printf("\n\nWorker thread: unsafe allocation!\n\n");
        //    stop_test = 1;
        //    break;
        //}
        //free(buf);
    }

    while(1);
}

/* funtion returns address of bootinfo struct */
seL4_BootInfo * get_bootinfo(void) {
    char *bootinfo_addr_str = getenv("bootinfo");
    ZF_LOGF_IF(bootinfo_addr_str == NULL, "Missing bootinfo environment variable");

    void *bootinfo;
    if (sscanf(bootinfo_addr_str, "%p", &bootinfo) != 1) {
        ZF_LOGF("bootinfo environment value '%s' was not valid.", bootinfo_addr_str);
    }

    return (seL4_BootInfo*)bootinfo;
}

int main(void) {
    int error;
    int i;

    /* get boot info */
    info = get_bootinfo();
    seL4_SetUserData((seL4_Word)info->ipcBuffer);

    /* Set up logging and give us a name: useful for debugging if the thread faults */
    zf_log_set_tag_prefix("hello-2:");
    name_thread(seL4_CapInitThreadTCB, "hello-2");
    
    seL4_TCB_SetPriority(seL4_CapInitThreadTCB, seL4_CapInitThreadTCB, seL4_MaxPrio);


    /* print out bootinfo */
    debug_print_bootinfo(info);

    /* get our cspace root cnode */
    seL4_CPtr cspace_cap;
    //cspace_cap = simple_get_cnode(&simple);
    cspace_cap = seL4_CapInitThreadCNode;

    /* get our vspace root page diretory */
    seL4_CPtr pd_cap;
    pd_cap = seL4_CapInitThreadPD;

    seL4_CPtr tcb_cap_start;
    /* TASK 1: Set tcb_cap to a free cap slot index.
     * hint: The bootinfo struct contains a range of free cap slot indices.
     */

    ep_cap_start = info->empty.start;
    tcb_cap_start = ep_cap_start+1;
    


    seL4_CPtr untyped;
    
    /* look up an untyped to retype into a tcb */
    untyped = get_untyped(info, NUM_WORKER_THREADS * BIT(seL4_TCBBits));
    ZF_LOGF_IFERR(untyped == -1, "Failed to find an untyped object of the right size.\n");


    /* create the tcb */
    error = seL4_Untyped_Retype(untyped /* untyped cap */,
                                seL4_TCBObject /* type */,
                                seL4_TCBBits /* size */,
                                cspace_cap /* root cnode cap */,
                                cspace_cap /* destination cspace */,
                                seL4_WordBits /* depth */,
                                tcb_cap_start /* offset */,
                                NUM_WORKER_THREADS /* num objects */);
    ZF_LOGF_IFERR(error, "Failed to allocate a TCB object.\n");


    untyped = get_untyped(info, BIT(seL4_EndpointBits));
    error = seL4_Untyped_Retype(untyped /* untyped cap */,
                                seL4_EndpointObject /* type */,
                                seL4_EndpointBits /* size */,
                                cspace_cap /* root cnode cap */,
                                cspace_cap /* destination cspace */,
                                seL4_WordBits /* depth */,
                                ep_cap_start /* offset */,
                                1 /* num objects */);
    ZF_LOGF_IFERR(error, "Failed to allocate an EP object.\n");

    for(i = 0; i < NUM_WORKER_THREADS; i++) {


        /* initialise the new TCB */
        error = seL4_TCB_Configure(tcb_cap_start + i, seL4_CapNull, 
                               cspace_cap, seL4_NilData, pd_cap, seL4_NilData, 0, 0);
        ZF_LOGF_IFERR(error, "Failed to configure TCB object.\n");

        seL4_TCB_SetPriority(tcb_cap_start + i, seL4_CapInitThreadTCB, seL4_MaxPrio);

        /* give the new thread a name */
        snprintf(worker_thread_names[i], MAX_NAME_LENGTH, "Worker thread %i", i);/* UNSAFE */
        name_thread(tcb_cap_start + i, worker_thread_names[i]);
    

        const int stack_alignment_requirement = sizeof(seL4_Word) * 2;
        uintptr_t worker_stack_top = (uintptr_t)worker_thread_stacks[i] + sizeof(worker_thread_stacks[i]);
        ZF_LOGF_IF(worker_stack_top % (stack_alignment_requirement) != 0,
                   "Stack top isn't aligned correctly to a %dB boundary.\n"
                   "\tDouble check to ensure you're not trampling.",
                    stack_alignment_requirement);

        /* set start up registers for the new thread: */
#ifdef CONFIG_ARCH_IA32
        /* set start up registers for the new thread: */
        seL4_UserContext regs = {
            .eip = (seL4_Word)worker_thread,
            .esp = (seL4_Word)worker_stack_top
        };
#elif defined(CONFIG_ARCH_ARM)
        seL4_UserContext regs = {
            .pc = (seL4_Word)worker_thread,
            .sp = (seL4_Word)worker_stack_top
        };
#elif defined(CONFIG_ARCH_X86_64)
        seL4_UserContext regs = {
            .rip = (seL4_Word)worker_thread,
            .rsp = (seL4_Word)worker_stack_top
        };
#else
#error "Unsupported architecture"
#endif

        error = seL4_TCB_WriteRegisters(tcb_cap_start + i, 0, 0, 2, &regs);

        ZF_LOGF_IFERR(error, "Failed to write the new thread's register set.\n");

        error = seL4_TCB_Resume(tcb_cap_start + i);
        ZF_LOGF_IFERR(error, "Failed to start new thread.\n");

        printf("Started thread %i\n", i);
        seL4_Yield();
    }

    printf("main: hello world\n");
    seL4_DebugDumpScheduler(); 
    while(1);

    return 0;
}
