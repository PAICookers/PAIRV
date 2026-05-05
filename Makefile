PROGRAM ?= application/baremetal/helloworld

PARALLEL ?=

.PHONY: __help ctags cleanall buildall tags
__help:
	@echo "Help about Build/Run/Debug/Clean PAIRV Nuclei N307FD Application"
	@echo "make [PROGRAM=/path/to/app] help                         Show the selected application's build help"
	@echo "make [EXTRA_APP_ROOTDIRS=/path/to/extraapps] cleanall    Clean all the applications"
	@echo "make [EXTRA_APP_ROOTDIRS=/path/to/extraapps] buildall    Rebuild all the applications"
	@echo "Examples:"
	@echo "make CORE=n307fd DOWNLOAD=ilm PROGRAM=application/baremetal/helloworld all"
	@echo "make PROGRAM=application/baremetal/helloworld help"
	@echo "make CORE=n307fd DOWNLOAD=ilmflashxip PROGRAM=application/baremetal/nice all"
	@echo "make -k cleanall"
	@echo "make -k buildall"

VALID_PROGRAM=$(wildcard $(PROGRAM))
VALID_PROGRAM_MAKEFILE=$(wildcard $(PROGRAM)/Makefile)

# Valid SDK Rules accepted by build system
VALID_SDK_RULES := all info help bin size dasm upload run_openocd run_gdb clean debug showflags showtoolver run_qemu run_xlspike run_xlmodel run_qemu_debug run_xlspike_rbb run_xlspike_openocd

# Default root directories to search
APP_ROOTDIRS := application
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

ifeq ($(VALID_PROGRAM_MAKEFILE), )
APP_PROGRAM=application/$(PROGRAM)
VALID_PROGRAM=$(wildcard $(APP_PROGRAM))
VALID_PROGRAM_MAKEFILE=$(wildcard $(APP_PROGRAM)/Makefile)
ifeq ($(VALID_PROGRAM_MAKEFILE), )
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

$(VALID_SDK_RULES):
	$(MAKE) -C $(VALID_PROGRAM) $@

# Only works in linux
# Exuberant Ctags or compatible ctags is required to be installed
tags ctags:
	ctags -o tags `find . -name '*.[chS]' -print`
	rm -f ctags
	ln -sf tags ctags
