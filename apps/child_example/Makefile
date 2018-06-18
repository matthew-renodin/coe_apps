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
LIBS = c sel4 sel4muslcsys sel4simple sel4vka sel4allocman sel4platsupport \
       platsupport utils sel4simple-default sel4utils sel4debug sel4vspace \
       process thread sync init


###############################################################################
# FLAGS
###############################################################################
CFLAGS += -Werror -g


###############################################################################
# COMMON INCLUDE
###############################################################################
include $(SEL4_COMMON)/common.mk
