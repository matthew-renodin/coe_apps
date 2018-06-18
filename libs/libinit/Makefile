###############################################################################
# libinit - Makefile
#
#
###############################################################################
PROTOC := protoc-c

PRIORITY_TARGETS := src/init_data.pb-c.h src/init_data.pb-c.c
TARGETS := libinit.a

###############################################################################
# SOURCE FILES
###############################################################################
CFILES := $(sort $(patsubst $(SOURCE_DIR)/%,%,$(wildcard $(SOURCE_DIR)/src/*.c))) \
          $(sort $(patsubst $(SOURCE_DIR)/%,%,$(wildcard $(SOURCE_DIR)/include/protobuf-c/*.c)))

HDRFILES := $(sort $(wildcard $(SOURCE_DIR)/include/*)) \
            $(sort $(wildcard ${SOURCE_DIR}/arch_include/${ARCH}/*)) \
            $(sort $(wildcard ${SOURCE_DIR}/src/*.h))


###############################################################################
# FLAGS
###############################################################################

###############################################################################
# COMMON INCLUDE
###############################################################################
include $(SEL4_COMMON)/common.mk


###############################################################################
# RULES
###############################################################################
src/init_data.pb-c.h src/init_data.pb-c.c: $(SOURCE_DIR)/src/init_data.proto
	@echo " [PROTOBUF-C] $@"
	$(PROTOC) --c_out=$(SOURCE_DIR)/src -I$(dir $<) $<


