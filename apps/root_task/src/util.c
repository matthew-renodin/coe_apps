
/* Include Kconfig variables. */
#include <autoconf.h>

#include <sel4/sel4.h>

/* avoid main falling off the end of the world */
void abort(void) {
    while (1);
}
