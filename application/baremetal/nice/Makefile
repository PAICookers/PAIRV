TARGET = nice

NUCLEI_SDK_ROOT = ../../..

SRCDIRS = . src

INCDIRS = . inc

COMMON_FLAGS := -Os

STDCLIB ?= newlib_small
DOWNLOAD ?= ilmflashxip

ifeq ($(DOWNLOAD),ilmflashxip)
else
$(error application/baremetal/nice only supports DOWNLOAD=ilmflashxip because it stores a large data set)
endif

# Link math library for sqrt, exp, etc.
LDLIBS += -lm

include $(NUCLEI_SDK_ROOT)/Build/Makefile.base
