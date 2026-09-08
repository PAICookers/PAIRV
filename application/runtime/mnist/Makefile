TARGET = mnist

NUCLEI_SDK_ROOT = ../../..
ASSET_DIR = assets
GEN_DIR = generated
ASSETS = compile_artifacts.bin
ASSET_OBJECTS = $(patsubst %.bin,$(GEN_DIR)/%_asset.o,$(ASSETS))
RVRT_APP_RX_FRAMES ?= 81
COMMON_FLAGS := -Os
COMMON_FLAGS += -DRVRT_APP_RX_FRAMES=$(RVRT_APP_RX_FRAMES)

INCDIRS = . $(NUCLEI_SDK_ROOT)/third_party/flatbuffers/include
SRCDIRS = .

ALL_OBJS += $(ASSET_OBJECTS)
CLEAN_OBJS += $(ASSET_OBJECTS)

$(GEN_DIR)/%.bin: $(ASSET_DIR)/%.bin
	mkdir -p $(dir $@)
	cp $< $@

$(GEN_DIR)/%_asset.o: $(GEN_DIR)/%.bin
	mkdir -p $(dir $@)
	$(OBJCOPY) -I binary -O elf32-littleriscv -B riscv \
		--rename-section .data=.large_const_data,alloc,load,readonly,data,contents \
		--set-section-alignment .large_const_data=8 \
		$< $@

include $(NUCLEI_SDK_ROOT)/Lib/runtime/build.mk
include $(NUCLEI_SDK_ROOT)/Build/Makefile.base

ifeq ($(filter ilmflashxip flashxip,$(DOWNLOAD)),)
$(error This app only supports DOWNLOAD=ilmflashxip or flashxip because it links the artifact into flash)
endif
