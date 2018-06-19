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

#include <init/init.h>
#include <process/process.h>



const process_attr_t process_default_attrs = {
    .heap_size_pages    = CONFIG_LIB_PROCESS_DEFAULT_HEAP_SIZE_PAGES,
    .stack_size_pages   = CONFIG_LIB_PROCESS_DEFAULT_STACK_SIZE_PAGES,
    .priority           = CONFIG_LIB_PROCESS_DEFAULT_PRIORITY,
    .cpu_affinity       = CONFIG_LIB_PROCESS_DEFAULT_CPU_AFFINITY,
    .cnode_size_bits    = CONFIG_LIB_PROCESS_DEFAULT_CNODE_SIZE_BITS,
};


int process_create(const char *elf_file_name,
                   const char *proc_name,
                   const process_attr_t *attr,
                   process_handle_t *handle)
{
    int error;

    if(!init_objects.initialized) return -1;

    if(handle == NULL) return -2; /* TODO come up with error codes */
    handle->running = 0;


    if(attr == NULL) {
        attr = &process_default_attrs;
    }

    /* TODO: Implement sync for init objects */
    error = vka_alloc_cnode_object(&init_objects.vka, attr->cnode_size_bits, &handle->cnode);

    
    
    

    return 0;
}


void process_run(process_handle_t *handle, int argc, char *argv[])
{
    /* TODO: Implement */
}


int process_add_device_pages(process_handle_t *handle, 
                             void *paddr,
                             seL4_Word num_pages,
                             const char* device_name)
{
    /* TODO: Implement */
    return 0;
}


int process_add_device_irq(process_handle_t *handle,
                           int irq_number,
                           const char* device_name)
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



int process_give_untyped_resources(process_handle_t *handle,
                                   seL4_Word length_bytes,
                                   seL4_Word num_objects)
{
    /* TODO: Implement */
    return 0;
}



