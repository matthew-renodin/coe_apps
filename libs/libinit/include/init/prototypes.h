/**
 * @file prototype.h
 * @brief Exported prototype definitions for libinit
 *
 */

#pragma once

/**
 * @brief Initializes the necessary userspace bookeeping for a single process.
 */
int init(void);

/**
 * @brief Initializes the necesarry bookeeping for the root task using seL4_BootInfo.
 */
int init_root_task(void);
