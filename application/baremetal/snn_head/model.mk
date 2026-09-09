# Complete-model build shared by the UART demo and board tests. Callers set
# SNN_HEAD_DIR and may narrow SNN_HEAD_ARTIFACTS to one layer.
SNN_HEAD_DEBUG ?= 0
SNN_HEAD_TIMING ?= 0
# Disposable outputs local to the executable being built; assets stay
# read-only.
SNN_HEAD_GENERATED_DIR := generated

ifneq ($(filter-out 0 1,$(SNN_HEAD_DEBUG)),)
$(error SNN_HEAD_DEBUG must be 0 or 1)
endif
ifneq ($(filter-out 0 1,$(SNN_HEAD_TIMING)),)
$(error SNN_HEAD_TIMING must be 0 or 1)
endif

override RV_DEBUG_ENABLE_LOGGING := $(SNN_HEAD_DEBUG)
override RVRT_ENABLE_STATS := $(SNN_HEAD_TIMING)

PAIRV_RUNTIME_DIR ?= $(NUCLEI_SDK_ROOT)/Lib/runtime
COMMON_FLAGS += -O2 -ffp-contract=off
COMMON_FLAGS += -DSNN_HEAD_TIMING=$(SNN_HEAD_TIMING)

SNN_HEAD_ARTIFACTS ?= fc1_lif block0_lif block1_lif fc2 fc3
SNN_HEAD_ARTIFACT_BINS = $(addsuffix /compile_artifacts.bin,$(addprefix $(SNN_HEAD_GENERATED_DIR)/,$(SNN_HEAD_ARTIFACTS)))
SNN_HEAD_ARTIFACT_OBJECTS = $(patsubst %/compile_artifacts.bin,%/compile_artifacts_asset.o,$(SNN_HEAD_ARTIFACT_BINS))

C_SRCS += \
	$(SNN_HEAD_DIR)/src/snn_head.c \
	$(SNN_HEAD_DIR)/src/snn_head_profile.c \
	$(SNN_HEAD_DIR)/src/snn_head_fc1_lif.c \
	$(SNN_HEAD_DIR)/src/snn_head_block_lif.c \
	$(SNN_HEAD_DIR)/src/snn_head_fc2.c \
	$(SNN_HEAD_DIR)/src/snn_head_fc3.c \
	$(SNN_HEAD_DIR)/src/snn_head_params.c \
	$(NUCLEI_SDK_ROOT)/Lib/debug.c \
	$(NUCLEI_SDK_ROOT)/Lib/nn_layernorm.c \
	$(NUCLEI_SDK_ROOT)/Lib/nn_quant.c

INCDIRS += $(SNN_HEAD_DIR)/include $(SNN_HEAD_DIR)/src $(NUCLEI_SDK_ROOT)/Lib \
	$(NUCLEI_SDK_ROOT)/third_party/flatbuffers/include
LDLIBS += -lm

include $(PAIRV_RUNTIME_DIR)/build.mk

ALL_OBJS += $(SNN_HEAD_ARTIFACT_OBJECTS)
CLEAN_OBJS += $(SNN_HEAD_GENERATED_DIR)

$(SNN_HEAD_GENERATED_DIR)/%/compile_artifacts.bin: $(SNN_HEAD_DIR)/assets/%/compile_artifacts.bin
	mkdir -p $(dir $@)
	cp $< $@

$(SNN_HEAD_GENERATED_DIR)/%/compile_artifacts_asset.o: $(SNN_HEAD_GENERATED_DIR)/%/compile_artifacts.bin
	mkdir -p $(dir $@)
	$(OBJCOPY) -I binary -O elf32-littleriscv -B riscv \
		--set-section-alignment .data=8 \
		--rename-section .data=.large_const_data,alloc,load,readonly,data,contents \
		--redefine-sym _binary_generated_$*_compile_artifacts_bin_start=snn_head_$*_artifact_start \
		--redefine-sym _binary_generated_$*_compile_artifacts_bin_end=snn_head_$*_artifact_end \
		--redefine-sym _binary_generated_$*_compile_artifacts_bin_size=snn_head_$*_artifact_size \
		$< $@
