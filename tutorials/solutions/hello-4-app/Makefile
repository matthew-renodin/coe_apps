#
# Copyright 2017, Data61
# Commonwealth Scientific and Industrial Research Organisation (CSIRO)
# ABN 41 687 119 230.
#
# This software may be distributed and modified according to the terms of
# the BSD 2-Clause license. Note that NO WARRANTY is provided.
# See "LICENSE_BSD2.txt" for details.
#
# @TAG(DATA61_BSD)
#

# Targets
TARGETS := $(notdir $(SOURCE_DIR)).bin

# Make sure this symbol stays around as we don't reference this, but
# whoever loads us will
LDFLAGS += -u __vsyscall_ptr

# Source files required to build the target	
CFILES :=  $(patsubst $(SOURCE_DIR)/%,%,$(wildcard $(SOURCE_DIR)/src/*.c))

#If we are starting other processes then we include the cpio archive.
#OFILES := archive.o
# If we don't start any processes, we need an empty filesystem/cpio
CFILES   += empty_cpio.c

# Libraries
LIBS := c sel4 sel4muslcsys sel4simple sel4vka sel4allocman sel4platsupport \
       platsupport utils sel4simple-default sel4utils sel4debug sel4vspace \
       elf cpio \
       process thread init mmap sel4sync lockwrapper

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
