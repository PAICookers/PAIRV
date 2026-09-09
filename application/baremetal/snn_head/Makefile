TARGET = snn_head

NUCLEI_SDK_ROOT := $(abspath ../../..)
SNN_HEAD_DIR := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))

BANNER ?= 1
SNN_HEAD_UART_IO_TIMEOUT_MS ?= 1000
SOC_DEBUG_UART_BAUDRATE ?= 3000000

COMMON_FLAGS += -DSNN_HEAD_UART_IO_TIMEOUT_MS=$(SNN_HEAD_UART_IO_TIMEOUT_MS)
include $(SNN_HEAD_DIR)/model.mk

ifeq ($(SNN_HEAD_DEBUG)$(SNN_HEAD_TIMING),11)
$(error SNN_HEAD_DEBUG and SNN_HEAD_TIMING are mutually exclusive)
endif

C_SRCS += $(SNN_HEAD_DIR)/main.c $(SNN_HEAD_DIR)/src/snn_head_uart.c

include $(NUCLEI_SDK_ROOT)/Build/Makefile.base

ifeq ($(filter ilmflashxip flashxip,$(DOWNLOAD)),)
$(error This app only supports DOWNLOAD=ilmflashxip or flashxip because it links SNN Head artifacts and parameters into flash)
endif
