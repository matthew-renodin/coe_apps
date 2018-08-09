/*
 * Copyright 2017, Data61
 * Commonwealth Scientific and Industrial Research Organisation (CSIRO)
 * ABN 41 687 119 230.
 *
 * This software may be distributed and modified according to the terms of
 * the BSD 2-Clause license. Note that NO WARRANTY is provided.
 * See "LICENSE_BSD2.txt" for details.
 *
 * @TAG(DATA61_BSD)
 */

/*
 * helper functions
 */

/* Include Kconfig variables. */
#include <autoconf.h>

#include <sel4/sel4.h>

/* Libinit wants to know something about CPIO data, and we don't need that here */
char _cpio_archive[] = {1};

/* avoid main falling off the end of the world */
void abort(void) {
    while (1);
}

