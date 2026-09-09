TARGET = snn_head_cpu_ops_selftest

NUCLEI_SDK_ROOT := $(abspath ../../../..)

INCDIRS = . $(NUCLEI_SDK_ROOT)/Lib

C_SRCS += main.c
C_SRCS += $(NUCLEI_SDK_ROOT)/Lib/nn_layernorm.c
C_SRCS += $(NUCLEI_SDK_ROOT)/Lib/nn_quant.c
C_SRCS += $(NUCLEI_SDK_ROOT)/Lib/nn_fc.c

COMMON_FLAGS := -Os -ffp-contract=off
LDLIBS += -lm

include $(NUCLEI_SDK_ROOT)/Build/Makefile.base
