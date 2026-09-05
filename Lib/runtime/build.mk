ifndef PAIRV_RUNTIME_BUILD_MK_INCLUDED
PAIRV_RUNTIME_BUILD_MK_INCLUDED := 1

RVRT_SESSION_ENABLE_STATS ?= 0

PAIRV_RUNTIME_DIR ?= $(PAIRV_SDK_LIB)/runtime

# Include this file after assigning COMMON_FLAGS so runtime sources inherit the
# application's optimization level and these definitions are appended once.
COMMON_FLAGS += -DRVRT_SESSION_ENABLE_STATS=$(RVRT_SESSION_ENABLE_STATS)

# Runtime is built as one unit. The SDK's section GC removes functions and
# static data that the selected application does not reference.
INCDIRS += $(PAIRV_RUNTIME_DIR)
C_SRCDIRS += $(PAIRV_RUNTIME_DIR)
CXX_SRCDIRS += $(PAIRV_RUNTIME_DIR)

endif
