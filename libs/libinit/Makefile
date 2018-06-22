###############################################################################
# libinit - Makefile
#
#
###############################################################################
PROTOC := protoc-c

PROTOBUF_TARGETS := include/init/init_data.pb-c.h include/init/init_data.pb-c.c

PRIORITY_TARGETS := $(PROTOBUF_TARGETS)
TARGETS := libinit.a
###############################################################################
# RULES
###############################################################################
	
###############################################################################
# SOURCE FILES
###############################################################################
CFILES := $(patsubst $(SOURCE_DIR)/%,%,$(wildcard $(SOURCE_DIR)/src/*.c)) \
          $(patsubst $(SOURCE_DIR)/%,%,$(wildcard $(SOURCE_DIR)/include/protobuf-c/*.c)) \
	  $(filter %.c,$(PRIORITY_TARGETS))
CFILES := $(sort $(CFILES))
		 

HDRFILES := $(wildcard $(SOURCE_DIR)/include/*) \
            $(wildcard ${SOURCE_DIR}/arch_include/${ARCH}/*) \
            $(wildcard ${SOURCE_DIR}/src/*.h) \
            $(filter %.h,$(PRIORITY_TARGETS))
HDRFILES := $(sort $(HDRFILES))


###############################################################################
# FLAGS
###############################################################################

###############################################################################
# COMMON INCLUDE
###############################################################################
include $(SEL4_COMMON)/common.mk


$(PROTOBUF_TARGETS): $(SOURCE_DIR)/include/init/init_data.proto
	@echo " [PROTOBUF-C] $@"
	mkdir -p ./include/init
	$(PROTOC) --c_out=./include/init -I$(dir $<) $<



