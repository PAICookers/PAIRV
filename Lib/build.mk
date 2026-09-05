PAIRV_SDK_LIB ?= $(NUCLEI_SDK_ROOT)/Lib
PAIRV_SDK_LIB_SRCS := $(sort $(wildcard $(PAIRV_SDK_LIB)/*.c))

# Logging is a global Lib facility. Runtime-specific configuration is only
# propagated by Lib/runtime/build.mk when an application opts into runtime.
RV_DEBUG_ENABLE_LOGGING ?= 0
COMMON_FLAGS += -DRV_DEBUG_ENABLE_LOGGING=$(RV_DEBUG_ENABLE_LOGGING)

INCDIRS += $(PAIRV_SDK_LIB)
C_SRCDIRS += $(PAIRV_SDK_LIB)
C_SRCS += $(PAIRV_SDK_LIB_SRCS)
