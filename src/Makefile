# src/Makefile

TOP = ..

include $(TOP)/configs/current

SUBDIRS = $(SRC_DIRS)

GLAPI_ROOT_PATH := $(shell pkg-config --variable=datadir glapi 2> /dev/null || echo NOTFOUND)

ifeq ($(strip $(GLAPI_ROOT_PATH)), NOTFOUND)

$(error "failed to find package `glapi'")

endif

export GLAPI_XML_PATH := $(GLAPI_ROOT_PATH)/xml/glapi

default: message $(TOP)/$(LIB_DIR) subdirs


message:
	@echo "Making sources for" $(CONFIG_NAME)


subdirs:
	@for dir in $(SUBDIRS) ; do \
		if [ -d $$dir ] ; then \
			(cd $$dir && $(MAKE)) || exit 1; \
		fi \
	done

install: $(TOP)/$(LIB_DIR)
	@for dir in $(SUBDIRS) ; do \
		if [ -d $$dir ] ; then \
			(cd $$dir && $(MAKE) install) || exit 1 ; \
		fi \
	done

$(TOP)/$(LIB_DIR):
	-@test -d $(TOP)/$(LIB_DIR) || mkdir -p $(TOP)/$(LIB_DIR)


clean:
	-@for dir in $(SUBDIRS) ; do \
		if [ -d $$dir ] ; then \
			(cd $$dir && $(MAKE) clean) ; \
		fi \
	done
