/**
 * @file globals.h
 * @brief Global declarations for libthread
 */

#pragma once

#include <sel4/sel4.h>

#include "types.h"

#define THREAD_SELF_CORE (-1)

extern const thread_attr_t thread_defaults_1MB_stack;
extern const thread_attr_t thread_defaults_64KB_stack;
