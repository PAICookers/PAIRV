TARGET = relay

NUCLEI_SDK_ROOT = ../../..
FRAME_DIR ?= frames
CONFIG_FRAME_FILE ?= $(FRAME_DIR)/config_frame.h
RELAY_HOST_WORD_ORDER ?= high
RV_DEBUG_ENABLE_LOGGING ?= 1

ifneq ($(filter high low,$(RELAY_HOST_WORD_ORDER)),$(RELAY_HOST_WORD_ORDER))
$(error RELAY_HOST_WORD_ORDER must be high or low)
endif

C_SRCS = main.c

COMMON_FLAGS := -Os -DRELAY_HOST_HIGH_FIRST=$(if $(filter high,$(RELAY_HOST_WORD_ORDER)),1,0)
ifneq ($(wildcard $(CONFIG_FRAME_FILE)),)
COMMON_FLAGS += -DRELAY_CONFIG_FRAME_HEADER=\"$(CONFIG_FRAME_FILE)\"
endif

include $(NUCLEI_SDK_ROOT)/Build/Makefile.base

ifeq ($(filter ilmflashxip flashxip,$(DOWNLOAD)),)
$(error only supports DOWNLOAD=ilmflashxip or flashxip because it stores a large data set)
endif
