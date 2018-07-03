/**
 * @file globals.h
 * @brief Global variable declarations for libmmap
 */

#pragma once

#include <sel4/sel4.h>
#include <utils/util.h>

#include "types.h"

extern const mmap_entry_attr_t mmap_attr_4k_code;
extern const mmap_entry_attr_t mmap_attr_4k_data;
extern const mmap_entry_attr_t mmap_attr_4k_readonly;
extern const mmap_entry_attr_t mmap_attr_4k_device;

