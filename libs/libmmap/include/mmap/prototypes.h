/*
 * Copyright 2018, Intelligent Automation, Inc.
 * This software was developed in part under Air Force contract number FA8750-15-C-0066 and DARPA 
 *  contract number 140D6318C0001.
 * This software was released under DARPA, public release number 1.0.
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
 * @file prototypes.h
 * @brief Exported prototype definitions for libmmap
 *
 */

#pragma once

#include <sel4/sel4.h>
#include <vka/vka.h>
#include <vspace/vspace.h>

#include "types.h"


int mmap_new_pages(seL4_Word num_pages,
                   const mmap_entry_attr_t *attr,
                   void **vaddr,
                   reservation_t *res);

int mmap_new_pages_custom(vspace_t *vspace,
                          seL4_CPtr vspace_root_cap,
                          seL4_Word num_pages,
                          const mmap_entry_attr_t *attr,
                          seL4_CPtr *caps,
                          void **vaddr,
                          reservation_t *res);

int mmap_new_device_pages_custom(vspace_t *vspace,
                                 seL4_CPtr vspace_root_cap,
                                 void *paddr,
                                 seL4_Word num_pages,
                                 const mmap_entry_attr_t *attr,
                                 seL4_CPtr *caps,
                                 void **vaddr,
                                 reservation_t *res);

int mmap_existing_pages_custom(vspace_t *vspace,
                               seL4_CPtr vspace_root_cap,
                               seL4_Word num_pages,
                               const mmap_entry_attr_t *attr,
                               seL4_CPtr *caps,
                               void **vaddr,
                               reservation_t *res);

int mmap_new_stack_custom(vspace_t *vspace,
                          seL4_CPtr vspace_root_cap,
                          seL4_Word num_pages,
                          void **vaddr,
                          reservation_t *res);
