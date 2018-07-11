###############################################################################
# Child Example - Makefile
#
#
###############################################################################

TARGETS := $(notdir $(SOURCE_DIR)).bin

###############################################################################
# SOURCE FILES
###############################################################################
CFILES   := $(patsubst $(SOURCE_DIR)/%,%,$(wildcard $(SOURCE_DIR)/src/*.c))
CFILES   += $(patsubst $(SOURCE_DIR)/%,%,$(wildcard $(SOURCE_DIR)/src/plat/${PLAT}/*.c))
CFILES   += $(patsubst $(SOURCE_DIR)/%,%,$(wildcard $(SOURCE_DIR)/src/arch/${ARCH}/*.c))
ASMFILES := $(patsubst $(SOURCE_DIR)/%,%,$(wildcard $(SOURCE_DIR)/src/arch/${ARCH}/*.S))
ASMFILES += $(patsubst $(SOURCE_DIR)/%,%,$(wildcard $(SOURCE_DIR)/src/plat/${PLAT}/*.S))


###############################################################################
# LIBRARIES
###############################################################################
LIBS = c sel4 sel4muslcsys sel4vka sel4allocman sel4simple sel4simple-default \
       utils sel4utils sel4debug sel4vspace platsupport sel4platsupport cpio elf \
       process thread sync sel4sync lockvka init mmap


###############################################################################
# FLAGS
###############################################################################
CFLAGS += -Werror -g
LDFLAGS += -u __vsyscall_ptr

###############################################################################
# COMMON INCLUDE
###############################################################################
include $(SEL4_COMMON)/common.mk

