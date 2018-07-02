/**
 * @file types.h
 * @brief Type definitions for libmmap
 */

#pragma once

#include <sel4/sel4.h>
#include <vka/vka.h>


typedef struct mmap_entry_attr {
    unsigned int page_size_bits  : 6;
    unsigned int readable        : 1;
    unsigned int writable        : 1;
    unsigned int executable      : 1;
    unsigned int cacheable       : 1;
} mmap_entry_attr_t;

