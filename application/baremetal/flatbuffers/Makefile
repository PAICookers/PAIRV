TARGET = flatbuffers

NUCLEI_SDK_ROOT = ../../..

FLATBUFFERS_INCLUDE_ROOT ?= $(NUCLEI_SDK_ROOT)/third_party/flatbuffers/include
ASSET_DIR = assets
COMPILE_ARTIFACTS = $(ASSET_DIR)/compile_artifacts.bin
ARTIFACT_OBJECT = generated/compile_artifacts_asset.o
PAIRV_RUNTIME_DIR = $(NUCLEI_SDK_ROOT)/Lib/runtime
COMMON_FLAGS := -Os

INCDIRS = . $(NUCLEI_SDK_ROOT)/Lib $(FLATBUFFERS_INCLUDE_ROOT)
SRCDIRS = .
C_SRCS += $(NUCLEI_SDK_ROOT)/Lib/debug.c

ALL_OBJS += $(ARTIFACT_OBJECT)
CLEAN_OBJS += $(ARTIFACT_OBJECT)

# Give the embedded bytes stable, app-prefixed symbols instead of exposing
# objcopy's path-derived _binary_assets_* names to C source.
$(ARTIFACT_OBJECT): $(COMPILE_ARTIFACTS)
	mkdir -p $(dir $@)
	$(OBJCOPY) -I binary -O elf32-littleriscv -B riscv \
		--rename-section .data=.large_const_data,alloc,load,readonly,data,contents \
		--set-section-alignment .large_const_data=8 \
		--redefine-sym _binary_assets_compile_artifacts_bin_start=rvrt_fb_artifact_start \
		--redefine-sym _binary_assets_compile_artifacts_bin_end=rvrt_fb_artifact_end \
		--redefine-sym _binary_assets_compile_artifacts_bin_size=rvrt_fb_artifact_size \
		$< $@

include $(PAIRV_RUNTIME_DIR)/build.mk
include $(NUCLEI_SDK_ROOT)/Build/Makefile.base

ifeq ($(filter ilmflashxip flashxip,$(DOWNLOAD)),)
$(error This app only supports DOWNLOAD=ilmflashxip or flashxip because it links compile_artifacts.bin into flash)
endif
