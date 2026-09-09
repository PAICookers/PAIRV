TARGET = snn_head_float_uart_test

NUCLEI_SDK_ROOT := $(abspath ../../../..)
SNN_HEAD_DIR := $(abspath ..)

BANNER ?= 1
SNN_HEAD_UART_IO_TIMEOUT_MS ?= 1000
SOC_DEBUG_UART_BAUDRATE ?= 3000000

COMMON_FLAGS := -O2
COMMON_FLAGS += -DSNN_HEAD_UART_IO_TIMEOUT_MS=$(SNN_HEAD_UART_IO_TIMEOUT_MS)

INCDIRS = $(SNN_HEAD_DIR)/include
C_SRCS += $(SNN_HEAD_DIR)/float_uart_test/main.c \
	$(SNN_HEAD_DIR)/src/snn_head_uart.c

include $(NUCLEI_SDK_ROOT)/Build/Makefile.base
