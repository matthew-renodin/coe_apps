###############################################################################
# Root Task - Makefile
#
#
###############################################################################

TARGETS := $(notdir $(SOURCE_DIR)).bin

ENTRY_POINT := _sel4_start


###############################################################################
# SOURCE FILES
###############################################################################
CFILES   := $(patsubst $(SOURCE_DIR)/%,%,$(wildcard $(SOURCE_DIR)/src/*.c))
CFILES   += $(patsubst $(SOURCE_DIR)/%,%,$(wildcard $(SOURCE_DIR)/src/plat/${PLAT}/*.c))
CFILES   += $(patsubst $(SOURCE_DIR)/%,%,$(wildcard $(SOURCE_DIR)/src/arch/${ARCH}/*.c))
ASMFILES := $(patsubst $(SOURCE_DIR)/%,%,$(wildcard $(SOURCE_DIR)/src/arch/${ARCH}/*.S))
ASMFILES += $(patsubst $(SOURCE_DIR)/%,%,$(wildcard $(SOURCE_DIR)/src/plat/${PLAT}/*.S))

#If we are starting other processes then we include the cpio archive.
#OFILES := archive.o
# If we don't start any processes, we need an empty filesystem/cpio
CFILES   += empty_cpio.c

###############################################################################
# LIBRARIES
###############################################################################
LIBS = c sel4 sel4muslcsys sel4simple sel4vka sel4allocman sel4platsupport \
       platsupport utils sel4simple-default sel4utils sel4debug sel4vspace \
       elf cpio \
       process thread init mmap sel4sync lockwrapper


###############################################################################
# FLAGS
###############################################################################
CFLAGS += -Werror -g


###############################################################################
# COMMON INCLUDE
###############################################################################
include $(SEL4_COMMON)/common.mk

${COMPONENTS}:
	false

archive.o: ${COMPONENTS}
	@echo " [GEN_CPIO] $@"
	$(Q)mkdir -p $(dir $@)
	${COMMON_PATH}/files_to_obj.sh $@ _cpio_archive $^

empty_cpio.c:
	@echo " [GEN_CPIO] $@"
	$(Q)cp ${COMMON_PATH}/empty_cpio.c .
