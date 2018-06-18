###############################################################################
# libinit - Makefile
#
#
###############################################################################
PROTOC := protoc-c

PRIORITY_TARGETS := src/init_data.pb-c.h src/init_data.pb-c.c
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


src/init_data.pb-c.h src/init_data.pb-c.c: $(SOURCE_DIR)/src/init_data.proto
	@echo " [PROTOBUF-C] $@"
	mkdir -p ./src
	$(PROTOC) --c_out=./src -I$(dir $<) $<



