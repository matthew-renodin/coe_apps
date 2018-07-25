/**
 * @file tls.c
 * @brief Implementing thread local storage functions
 *
 */

#include <autoconf.h>

#include <stdint.h>

#include <sel4/sel4.h>

#include <init/init.h>

/**
 * TODO: Move these implementations into arch folders/files.
 */

int init_set_thread_local_storage(void *storage)
{
#ifdef CONFIG_ARCH_AARCH64
    __asm __volatile ("msr tpidr_el0, %0" :: "r" (storage));
#endif
#ifdef CONFIG_ARCH_AARCH32
#ifndef CONFIG_IPC_BUF_TPIDRURW
    __asm __volatile ("mcr p15, 0, %0, c13, c0, 2" :: "r" (storage));
#else
    ZF_LOGF("Not implemented! Please disable CONFIG_IPC_BUF_TPIDRURW.");
#endif
#endif
#ifdef CONFIG_ARCH_X86_64
    /* TODO */
    ZF_LOGW("Not implemented yet!");
#endif
#ifdef CONFIG_ARCH_IA32
    /* TODO */
    ZF_LOGW("Not implemented yet!");
#endif
    return 0;
}


void *init_get_thread_local_storage(void)
{
    void *ret = NULL;
#ifdef CONFIG_ARCH_AARCH64
    __asm __volatile ("mrs %0, tpidr_el0"  : "=r" (ret) ::);
#endif
#ifdef CONFIG_ARCH_AARCH32
#ifndef CONFIG_IPC_BUF_TPIDRURW
    __asm __volatile ("mrc p15, 0, %0, c13, c0, 2" : "=r" (ret) ::);
#else
    ZF_LOGF("Not implemented! Please disable CONFIG_IPC_BUF_TPIDRURW.");
#endif
#endif
#ifdef CONFIG_ARCH_X86_64
    /* TODO */
    ZF_LOGE("Not implemented yet!");
#endif
#ifdef CONFIG_ARCH_IA32
    /* TODO */
    ZF_LOGE("Not implemented yet!");
#endif
    return ret;
}


