TARGET = uart

NUCLEI_SDK_ROOT = ../../..
FRAME_DIR ?= include/frames

SRCDIRS = . src

INCDIRS = include $(FRAME_DIR)

COMMON_FLAGS := -Os
RV_DEBUG_ENABLE_LOGGING ?= 1

include $(NUCLEI_SDK_ROOT)/Build/Makefile.base

ifeq ($(filter ilmflashxip flashxip,$(DOWNLOAD)),)
$(error only supports DOWNLOAD=ilmflashxip or flashxip because it stores a large data set)
endif
