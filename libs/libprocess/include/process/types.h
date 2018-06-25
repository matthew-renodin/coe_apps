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
 * @file types.h
 * @brief Exported type definitions for libprocess
 *
 */

#pragma once


#include <sel4/sel4.h>
#include <vka/vka.h>
#include <vspace/vspace.h>
#include <sel4utils/vspace.h>
#include <sel4utils/elf.h>


#include <init/init.h>
#include <thread/thread.h>
/**
 *
 */
typedef struct process_attr {
    seL4_Word heap_size_pages;
    seL4_Word stack_size_pages;

    seL4_Word priority;
    seL4_Word cpu_affinity;

    seL4_Word cnode_size_bits;
} process_attr_t;


/**
 * @brief Userspace bookeeping for a child process resources.
 */
typedef struct process_handle {
    /* Only one thread can modify this structure at once */
    //int lock;
    int running;

    const char *name;

    void* entry_point;
    int num_elf_phdrs;
    Elf_Phdr *elf_phdrs;
    uintptr_t sysinfo;

    process_attr_t attrs;

    vka_object_t cnode;
    vka_object_t fault_ep;
    vka_object_t page_dir;

    vspace_t vspace;
    sel4utils_alloc_data_t vspace_data;

    seL4_Word cnode_root_data;
    int cnode_next_free;

    thread_handle_t main_thread;

    InitData init_data;
    

} process_handle_t;

