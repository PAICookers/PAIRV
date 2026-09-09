# Shared board-test build. The child directory names the target and artifact.
SNN_HEAD_BOARD_TEST_DIR := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
SNN_HEAD_DIR := $(abspath $(SNN_HEAD_BOARD_TEST_DIR)/..)
NUCLEI_SDK_ROOT ?= $(abspath $(SNN_HEAD_DIR)/../../..)
SNN_HEAD_TEST := $(notdir $(CURDIR))
TARGET := snn_head_test_$(SNN_HEAD_TEST)

override SNN_HEAD_TIMING := 1
DUMP ?= 0

ifneq ($(filter-out 0 1,$(DUMP)),)
$(error DUMP must be 0 or 1)
endif

ifeq ($(DUMP),1)
COMMON_FLAGS += -DSNN_HEAD_ENABLE_DUMP
endif

ifneq ($(SNN_HEAD_TEST),chain)
SNN_HEAD_ARTIFACTS := $(SNN_HEAD_TEST)
endif

INCDIRS += $(SNN_HEAD_BOARD_TEST_DIR)/golden
C_SRCS += main.c $(SNN_HEAD_BOARD_TEST_DIR)/golden/snn_head_golden.c \
	$(SNN_HEAD_BOARD_TEST_DIR)/snn_head_dump_sink.c
include $(SNN_HEAD_DIR)/model.mk

include $(NUCLEI_SDK_ROOT)/Build/Makefile.base

ifeq ($(filter ilmflashxip flashxip,$(DOWNLOAD)),)
$(error This app only supports DOWNLOAD=ilmflashxip or flashxip because it links SNN Head artifacts and parameters into flash)
endif
