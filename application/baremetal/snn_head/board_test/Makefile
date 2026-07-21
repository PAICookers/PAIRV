# 选择要单独上板测试的层：make LAYER=fc1_lif|block0_lif|block1_lif|fc2|fc3
# 每层是一个独立可执行程序（各自 main 在 layers/test_$(LAYER).c）。
LAYER ?= fc1_lif
TARGET = snn_head_test_$(LAYER)

# board_test 是 snn_head 的子目录，仓库根还要再上 4 层。
NUCLEI_SDK_ROOT = ../../../..

# 父目录 snn_head：层实现、CPU 参数、按层 artifact fixtures 都在这里。
SNN_HEAD_DIR = ..
FLATBUFFERS_INCLUDE_ROOT ?= $(NUCLEI_SDK_ROOT)/third_party/flatbuffers/include
FIXTURE_DIR = $(SNN_HEAD_DIR)/fixtures
GEN_DIR = generated
PAIRV_RUNTIME_DIR = $(NUCLEI_SDK_ROOT)/Lib/runtime

# 复用 snn_head 的按层 PAICORE artifact。fc3 fixture 到位后自动纳入（无需改此文件）：
# - 在此之前，非 fc3 的构建里 run_chunk/snn_head_run_fc3 未被引用，--gc-sections
#   会回收它们及对 fc3 artifact 符号的引用，因此其余 4 层照常链接；
# - LAYER=fc3 需要 fixtures/fc3/compile_artifacts.bin 存在才能链接。
SNN_HEAD_ARTIFACTS = fc1_lif block0_lif block1_lif fc2
ifneq ($(wildcard $(FIXTURE_DIR)/fc3/compile_artifacts.bin),)
SNN_HEAD_ARTIFACTS += fc3
endif
SNN_HEAD_ARTIFACT_BINS = $(addsuffix /compile_artifacts.bin,$(addprefix $(GEN_DIR)/,$(SNN_HEAD_ARTIFACTS)))
SNN_HEAD_ARTIFACT_OBJECTS = $(patsubst %/compile_artifacts.bin,%/compile_artifacts_asset.o,$(SNN_HEAD_ARTIFACT_BINS))

# CPU 侧参数文件（LN/quant/dequant 参数）来自 snn_head 目录。
SNN_HEAD_PARAM_SRCS ?= $(wildcard $(SNN_HEAD_DIR)/snn_head_params.c)

# SRCDIRS 只收本目录（无 main）：golden 数据（snn_head_golden.c）。
# 被选中的单层 main 在 layers/ 子目录（不被自动收），下面显式加入。
SRCDIRS = .
INCDIRS = . $(SNN_HEAD_DIR) $(NUCLEI_SDK_ROOT)/Lib $(PAIRV_RUNTIME_DIR) $(FLATBUFFERS_INCLUDE_ROOT)

# 本次构建的单层测试 main（只此一个 main，避免多 main 冲突）。
C_SRCS += layers/test_$(LAYER).c

# 共享的五层实现与参数显式列出（在父目录，SRCDIRS 不会自动收）。
C_SRCS += $(SNN_HEAD_PARAM_SRCS)
C_SRCS += $(SNN_HEAD_DIR)/snn_head.c
C_SRCS += $(SNN_HEAD_DIR)/snn_head_fc1_lif.c
C_SRCS += $(SNN_HEAD_DIR)/snn_head_block_lif.c
C_SRCS += $(SNN_HEAD_DIR)/snn_head_fc2.c
C_SRCS += $(SNN_HEAD_DIR)/snn_head_fc3.c
C_SRCS += $(NUCLEI_SDK_ROOT)/Lib/debug.c
C_SRCS += $(NUCLEI_SDK_ROOT)/Lib/nn_layernorm.c
C_SRCS += $(NUCLEI_SDK_ROOT)/Lib/nn_quant.c
C_SRCS += $(PAIRV_RUNTIME_DIR)/frame_codec.c
C_SRCS += $(PAIRV_RUNTIME_DIR)/session.c
C_SRCS += $(PAIRV_RUNTIME_DIR)/session_io.c
CXX_SRCS += $(PAIRV_RUNTIME_DIR)/artifact_reader.cpp

COMMON_FLAGS := -O2 -ffp-contract=off
COMMON_FLAGS += -isystem $(NUCLEI_TOOL_ROOT)/gcc/lib/gcc/riscv64-unknown-elf/14.2.1/include

# 逐层中间结果打印：make ... DUMP=1 打开（默认关闭，对部署路径逐字节零影响）。
# 打开后各层在 ②LN/③量化int8/④PAICore原始输出 阶段调用 snn_head_dump_sink.c 全量打印。
ifeq ($(DUMP),1)
COMMON_FLAGS += -DSNN_HEAD_ENABLE_DUMP
endif

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
