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
 * @file process.c
 * @brief Core libprocess implementation.
 *
 */

#include <sel4/sel4.h>

#include <process/process.h>

int process_create(const char *elf_file_name,
                   const char *proc_name,
                   seL4_Word heap_size_pages,
                   seL4_Word stack_size_pages,
                   seL4_Word priority,
                   seL4_Word cpu_affinity,
                   process_handle_t *handle)
{
    /* TODO: Implement */
    return 0;
}


void process_run(process_handle_t *handle)
{
    /* TODO: Implement */
}


int process_add_device_pages(process_handle_t *handle, void *paddr, seL4_Word num_pages)
{
    /* TODO: Implement */
    return 0;
}


int process_add_device_irq(process_handle_t *handle, int irq_number)
{
    /* TODO: Implement */
    return 0;
}


int process_connect_ep(process_handle_t *handle1, seL4_CapRights_t perms1,
                       process_handle_t *handle2, seL4_CapRights_t perms2,
                       const char *conn_name)
{
    /* TODO: Implement */
    return 0;
}


int process_connect_shmem(process_handle_t *handle1, seL4_CapRights_t perms1, 
                          process_handle_t *handle2, seL4_CapRights_t perms2,
                          seL4_Word num_pages,
                          const char *conn_name)
{
    /* TODO: Implement */
    return 0;
}


int process_connect_notification(process_handle_t *handle1, seL4_CapRights_t perms1,
                                 process_handle_t *handle2, seL4_CapRights_t perms2,
                                 const char *conn_name)
{
    /* TODO: Implement */
    return 0;
}



int process_give_untyped_memory(process_handle_t *handle, seL4_Word length_bytes)
{
    /* TODO: Implement */
    return 0;
}

