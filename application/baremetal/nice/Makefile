TARGET = nice

NUCLEI_SDK_ROOT = ../../..

SRCDIRS = . src

INCDIRS = . inc

COMMON_FLAGS := -Os

# Link math library for sqrt, exp, etc.
LDLIBS += -lm

include $(NUCLEI_SDK_ROOT)/Build/Makefile.base

ifneq ($(DOWNLOAD),ilmflashxip)
ifneq ($(DOWNLOAD),flashxip)
$(error only supports DOWNLOAD=ilmflashxip or flashxip because it stores a large data set)
endif
endif
