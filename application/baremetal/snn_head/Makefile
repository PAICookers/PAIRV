TARGET = snn_head

NUCLEI_SDK_ROOT = ../../..

FLATBUFFERS_INCLUDE_ROOT ?= $(NUCLEI_SDK_ROOT)/third_party/flatbuffers/include
FIXTURE_DIR = fixtures
GEN_DIR = generated
PAIRV_RUNTIME_DIR = $(NUCLEI_SDK_ROOT)/Lib/runtime

# 当前已到位的按层 PAICore artifact；fc3 到位后追加到这里即可。
SNN_HEAD_ARTIFACTS = fc1_lif block0_lif block1_lif fc2
SNN_HEAD_ARTIFACT_BINS = $(addsuffix /compile_artifacts.bin,$(addprefix $(GEN_DIR)/,$(SNN_HEAD_ARTIFACTS)))
SNN_HEAD_ARTIFACT_OBJECTS = $(patsubst %/compile_artifacts.bin,%/compile_artifacts_asset.o,$(SNN_HEAD_ARTIFACT_BINS))

# CPU 侧参数文件中的大数组/标量应使用 section(".large_const_data"), aligned(8)。
SNN_HEAD_PARAM_SRCS ?= $(wildcard snn_head_params.c)

SRCDIRS = .
INCDIRS = . $(NUCLEI_SDK_ROOT)/Lib $(PAIRV_RUNTIME_DIR) $(FLATBUFFERS_INCLUDE_ROOT)

C_SRCS += $(SNN_HEAD_PARAM_SRCS)
C_SRCS += $(NUCLEI_SDK_ROOT)/Lib/debug.c
C_SRCS += $(NUCLEI_SDK_ROOT)/Lib/nn_layernorm.c
C_SRCS += $(NUCLEI_SDK_ROOT)/Lib/nn_quant.c
C_SRCS += $(PAIRV_RUNTIME_DIR)/frame_codec.c
C_SRCS += $(PAIRV_RUNTIME_DIR)/session.c
C_SRCS += $(PAIRV_RUNTIME_DIR)/session_io.c
CXX_SRCS += $(PAIRV_RUNTIME_DIR)/artifact_reader.cpp

COMMON_FLAGS := -O2 -ffp-contract=off
COMMON_FLAGS += -isystem $(NUCLEI_TOOL_ROOT)/gcc/lib/gcc/riscv64-unknown-elf/14.2.1/include

LDLIBS += -lm

ALL_OBJS += $(SNN_HEAD_ARTIFACT_OBJECTS)
CLEAN_OBJS += $(SNN_HEAD_ARTIFACT_OBJECTS) $(SNN_HEAD_ARTIFACT_BINS)

$(GEN_DIR)/%/compile_artifacts.bin: $(FIXTURE_DIR)/%/compile_artifacts.bin
	mkdir -p $(dir $@)
	cp $< $@

$(GEN_DIR)/%/compile_artifacts_asset.o: $(GEN_DIR)/%/compile_artifacts.bin
	mkdir -p $(dir $@)
	$(OBJCOPY) -I binary -O elf32-littleriscv -B riscv \
		--rename-section .data=.large_const_data,alloc,load,readonly,data,contents \
		--set-section-alignment .large_const_data=8 \
		--redefine-sym _binary_generated_$*_compile_artifacts_bin_start=snn_head_$*_artifact_start \
		--redefine-sym _binary_generated_$*_compile_artifacts_bin_end=snn_head_$*_artifact_end \
		--redefine-sym _binary_generated_$*_compile_artifacts_bin_size=snn_head_$*_artifact_size \
		$< $@

include $(NUCLEI_SDK_ROOT)/Build/Makefile.base

ifneq ($(DOWNLOAD),ilmflashxip)
ifneq ($(DOWNLOAD),flashxip)
$(error This app only supports DOWNLOAD=ilmflashxip or flashxip because it links SNN Head artifacts and parameters into flash)
endif
endif
