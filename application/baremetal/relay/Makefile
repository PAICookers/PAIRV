TARGET = relay

NUCLEI_SDK_ROOT = ../../..
FRAME_DIR ?= frames
CONFIG_FRAME_FILE ?= $(FRAME_DIR)/config_frame.h
RELAY_HOST_WORD_ORDER ?= high
ENABLE_HOST_CONFIG ?= 0
RV_DEBUG_ENABLE_LOGGING ?= 1

ifneq ($(filter high low,$(RELAY_HOST_WORD_ORDER)),$(RELAY_HOST_WORD_ORDER))
$(error RELAY_HOST_WORD_ORDER must be high or low)
endif

ifneq ($(filter 0 1,$(ENABLE_HOST_CONFIG)),$(ENABLE_HOST_CONFIG))
$(error ENABLE_HOST_CONFIG must be 0 or 1)
endif

C_SRCS += main.c relay_protocol.c

COMMON_FLAGS := -Os -DENABLE_HOST_CONFIG=$(ENABLE_HOST_CONFIG) \
                -DRELAY_HOST_HIGH_FIRST=$(if $(filter high,$(RELAY_HOST_WORD_ORDER)),1,0)
ifeq ($(ENABLE_HOST_CONFIG),0)
ifeq ($(wildcard $(CONFIG_FRAME_FILE)),)
$(error preloaded configuration requires CONFIG_FRAME_FILE)
else
COMMON_FLAGS += -DRELAY_CONFIG_FRAME_HEADER=\"$(CONFIG_FRAME_FILE)\"
endif
endif

include $(NUCLEI_SDK_ROOT)/Build/Makefile.base

ifeq ($(filter ilmflashxip flashxip,$(DOWNLOAD)),)
$(error only supports DOWNLOAD=ilmflashxip or flashxip because it stores a large data set)
endif

.PHONY: host-test
host-test:
	cc -std=c11 -Wall -Wextra -Werror -DENABLE_HOST_CONFIG=1 \
		-DRELAY_HOST_HIGH_FIRST=1 -I. relay_protocol.c tests/test_relay_protocol.c \
		-o tests/out.o
	./tests/out.o
