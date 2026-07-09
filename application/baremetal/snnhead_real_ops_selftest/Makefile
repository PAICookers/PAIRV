TARGET = snnhead_real_ops_selftest

NUCLEI_SDK_ROOT = ../../..

SRCDIRS = .
INCDIRS = . $(NUCLEI_SDK_ROOT)/Lib

C_SRCS += $(NUCLEI_SDK_ROOT)/Lib/nn_layernorm.c
C_SRCS += $(NUCLEI_SDK_ROOT)/Lib/nn_quant.c
C_SRCS += $(NUCLEI_SDK_ROOT)/Lib/nn_fc.c

COMMON_FLAGS := -O2 -ffp-contract=off
COMMON_FLAGS += -isystem $(NUCLEI_TOOL_ROOT)/gcc/lib/gcc/riscv64-unknown-elf/14.2.1/include

LDLIBS += -lm

include $(NUCLEI_SDK_ROOT)/Build/Makefile.base
