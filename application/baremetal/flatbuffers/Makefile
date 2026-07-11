TARGET = flatbuffers

NUCLEI_SDK_ROOT = ../../..

FLATBUFFERS_INCLUDE_ROOT ?= $(NUCLEI_SDK_ROOT)/third_party/flatbuffers/include
FIXTURE_DIR = fixtures
COMPILE_ARTIFACTS = $(FIXTURE_DIR)/compile_artifacts.bin
ARTIFACT_OBJECT = generated/compile_artifacts_asset.o
PAIRV_RUNTIME_DIR = $(NUCLEI_SDK_ROOT)/Lib/runtime

SRCDIRS = .
C_SRCS += $(NUCLEI_SDK_ROOT)/Lib/debug.c
CXX_SRCS += $(PAIRV_RUNTIME_DIR)/artifact_reader.cpp

INCDIRS = . $(NUCLEI_SDK_ROOT)/Lib $(PAIRV_RUNTIME_DIR) $(FLATBUFFERS_INCLUDE_ROOT)

COMMON_FLAGS := -Os

ALL_OBJS += $(ARTIFACT_OBJECT)
CLEAN_OBJS += $(ARTIFACT_OBJECT)

# Give the embedded bytes stable, app-prefixed symbols instead of exposing
# objcopy's path-derived _binary_fixtures_* names to C source.
$(ARTIFACT_OBJECT): $(COMPILE_ARTIFACTS)
	mkdir -p $(dir $@)
	$(OBJCOPY) -I binary -O elf32-littleriscv -B riscv \
		--rename-section .data=.large_const_data,alloc,load,readonly,data,contents \
		--set-section-alignment .large_const_data=8 \
		--redefine-sym _binary_fixtures_compile_artifacts_bin_start=rvrt_fb_artifact_start \
		--redefine-sym _binary_fixtures_compile_artifacts_bin_end=rvrt_fb_artifact_end \
		--redefine-sym _binary_fixtures_compile_artifacts_bin_size=rvrt_fb_artifact_size \
		$< $@

include $(NUCLEI_SDK_ROOT)/Build/Makefile.base

ifneq ($(DOWNLOAD),ilmflashxip)
ifneq ($(DOWNLOAD),flashxip)
$(error This app only supports DOWNLOAD=ilmflashxip or flashxip because it links compile_artifacts.bin into flash)
endif
endif
