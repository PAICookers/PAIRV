TARGET = proto_flash

NUCLEI_SDK_ROOT = ../../..

PROTOC ?= protoc
PROTO_DIR = proto
PROTO_FILES = $(wildcard $(PROTO_DIR)/*.proto)
PROTO_GEN_DIR = generated
PROTO_HDR := $(patsubst $(PROTO_DIR)/%.proto,$(PROTO_GEN_DIR)/%.h,$(PROTO_FILES))
EMBEDDED_PROTO_DIR = $(NUCLEI_SDK_ROOT)/third_party/EmbeddedProto
PROTOC_GEN_EAMS = $(EMBEDDED_PROTO_DIR)/protoc-gen-eams

SRCDIRS = . $(EMBEDDED_PROTO_DIR)/src ../../../common

INCDIRS = . $(PROTO_GEN_DIR) $(EMBEDDED_PROTO_DIR)/src ../../../common

COMMON_FLAGS := -Os

CLEAN_OBJS += $(PROTO_HDR)
MAKEFILE_PREREQS += $(PROTO_HDR)

PB_ASSET = $(PROTO_DIR)/test_message.pb
PB_OBJECT = $(PROTO_GEN_DIR)/test_message_asset.o
ALL_OBJS += $(PB_OBJECT)
CLEAN_OBJS += $(PB_OBJECT)

.PHONY: generate

generate: $(PROTO_HDR)
	$(info Done generating source files based on *.proto files.)

$(PROTO_GEN_DIR)/%.h: $(PROTO_DIR)/%.proto
	mkdir -p $(dir $@)
	$(PROTOC) --plugin=protoc-gen-eams=$(PROTOC_GEN_EAMS) -I$(PROTO_DIR) --eams_out=$(PROTO_GEN_DIR) $<

$(PB_OBJECT): $(PB_ASSET)
	mkdir -p $(dir $@)
	$(OBJCOPY) -I binary -O elf32-littleriscv -B riscv \
		--rename-section .data=.large_const_data,alloc,load,readonly,data,contents \
		$< $@

include $(NUCLEI_SDK_ROOT)/Build/Makefile.base

ifneq ($(DOWNLOAD),ilmflashxip)
ifneq ($(DOWNLOAD),flashxip)
$(error proto_flash only supports DOWNLOAD=ilmflashxip or flashxip because it links a protobuf asset into flash)
endif
endif
