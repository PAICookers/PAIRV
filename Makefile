PROGRAM ?= application/baremetal/helloworld

.DEFAULT_GOAL := help

# Targets forwarded into a concrete application directory.
APP_TARGETS := all info bin size dasm upload run_openocd run_gdb clean debug showflags showtoolver run_qemu run_xlspike run_xlmodel run_qemu_debug run_xlspike_rbb run_xlspike_openocd

.PHONY: help __help cleanall buildall $(APP_TARGETS)
help: __help

__help:
	@echo "Help about Build/Run/Debug/Clean PAIRV Nuclei N307FD Application"
	@echo "make [PROGRAM=/path/to/app] help                         Show this top-level help message"
	@echo "make [EXTRA_APP_ROOTDIRS=/path/to/extraapps] cleanall    Clean all the applications"
	@echo "make [EXTRA_APP_ROOTDIRS=/path/to/extraapps] buildall    Rebuild all the applications"
	@echo "Examples:"
	@echo "make CORE=n307fd DOWNLOAD=ilm PROGRAM=application/baremetal/helloworld all"
	@echo "make CORE=n307fd DOWNLOAD=ilmflashxip PROGRAM=application/baremetal/nice all"
	@echo "make CORE=n307fd DOWNLOAD=ilm PROGRAM=tests/softmax_test clean"
	@echo "make CORE=n307fd DOWNLOAD=ilm PROGRAM=tests/softmax_test all"
	@echo "make cleanall"
	@echo "make buildall"

# Default root directories to search
APP_ROOTDIRS := application tests
# Extra application root directories passed by make
EXTRA_APP_ROOTDIRS ?=

# get all the root directories for applications
TOTAL_ROOTDIRS := $(APP_ROOTDIRS) $(EXTRA_APP_ROOTDIRS)

# Default search patterns
SEARCH_PATTERNS := * */* */*/* */*/*/*

PROGS_TO_SEARCH := $(foreach rootdir, $(TOTAL_ROOTDIRS), $(addprefix $(rootdir)/, $(SEARCH_PATTERNS)))
PROGS_makefile := $(foreach progdir, $(PROGS_TO_SEARCH), $(sort $(dir $(wildcard $(progdir)/makefile))))
PROGS_Makefile := $(foreach progdir, $(PROGS_TO_SEARCH), $(sort $(dir $(wildcard $(progdir)/Makefile))))
PROGS_DIRS := $(sort $(PROGS_makefile) $(PROGS_Makefile))
CLEAN_DIRS_RULES := $(addprefix __CLEAN__, $(PROGS_DIRS))
BUILD_DIRS_RULES := $(addprefix __BUILD__, $(PROGS_DIRS))

VALID_PROGRAM_MAKEFILES := $(wildcard $(PROGRAM)/Makefile $(PROGRAM)/makefile)
VALID_PROGRAM := $(patsubst %/,%,$(dir $(firstword $(VALID_PROGRAM_MAKEFILES))))

ifneq ($(filter $(APP_TARGETS),$(MAKECMDGOALS)),)
ifeq ($(VALID_PROGRAM),)
$(error No valid Makefile in $(PROGRAM) directory! please check!)
endif
endif

cleanall: $(CLEAN_DIRS_RULES)

buildall: $(BUILD_DIRS_RULES)

$(CLEAN_DIRS_RULES):
	$(MAKE) -C $(patsubst __CLEAN__%, %, $@) clean

$(BUILD_DIRS_RULES):
	$(MAKE) -C $(patsubst __BUILD__%, %, $@) clean
	$(MAKE) -C $(patsubst __BUILD__%, %, $@) $(PARALLEL) all

$(APP_TARGETS):
	$(MAKE) -C $(VALID_PROGRAM) $@
