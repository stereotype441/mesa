# This file isn't used during a normal compilation since we don't want to
# require Python in order to compile Mesa.
# Instead, when the Mesa developers update/change the API interface it's
# up to him/her to re-run this makefile and check in the newly generated files.


TOP = ../../../..
include $(TOP)/configs/current

MESA_DIR = $(TOP)/src/mesa
MESA_GLAPI_DIR = $(TOP)/src/mapi/glapi
MESA_MAPI_DIR = $(TOP)/src/mapi/mapi
MESA_GLX_DIR = $(TOP)/src/glx

MESA_GLAPI_OUTPUTS = \
	$(MESA_GLAPI_DIR)/glapi_mapi_tmp.h \
	$(MESA_GLAPI_DIR)/glprocs.h \
	$(MESA_GLAPI_DIR)/glapitemp.h \
	$(MESA_GLAPI_DIR)/glapitable.h \
	$(MESA_GLAPI_DIR)/glapi_gentable.c

MESA_GLAPI_ASM_OUTPUTS = \
	$(MESA_GLAPI_DIR)/glapi_x86.S \
	$(MESA_GLAPI_DIR)/glapi_x86-64.S \
	$(MESA_GLAPI_DIR)/glapi_sparc.S

MESA_OUTPUTS = \
	$(MESA_GLAPI_OUTPUTS) \
	$(MESA_GLAPI_ASM_OUTPUTS) \
	$(MESA_DIR)/main/enums.c \
	$(MESA_DIR)/main/dispatch.h \
	$(MESA_DIR)/main/remap_helper.h \
	$(MESA_GLX_DIR)/indirect.c \
	$(MESA_GLX_DIR)/indirect.h \
	$(MESA_GLX_DIR)/indirect_init.c \
	$(MESA_GLX_DIR)/indirect_size.h \
	$(MESA_GLX_DIR)/indirect_size.c

######################################################################

XORG_GLX_DIR = $(XORG_BASE)/glx
XORG_GLAPI_DIR = $(XORG_BASE)/glx

XORG_GLAPI_OUTPUTS = \
	$(XORG_GLAPI_DIR)/glprocs.h \
	$(XORG_GLAPI_DIR)/glapitable.h \
	$(XORG_GLAPI_DIR)/dispatch.h \
	$(XORG_GLAPI_DIR)/glapi_gentable.c

XORG_OUTPUTS = \
	$(XORG_GLAPI_OUTPUTS) \
	$(XORG_GLX_DIR)/indirect_dispatch.c \
	$(XORG_GLX_DIR)/indirect_dispatch_swap.c \
	$(XORG_GLX_DIR)/indirect_dispatch.h \
	$(XORG_GLX_DIR)/indirect_reqsize.c \
	$(XORG_GLX_DIR)/indirect_reqsize.h \
	$(XORG_GLX_DIR)/indirect_size.h \
	$(XORG_GLX_DIR)/indirect_size_get.c \
	$(XORG_GLX_DIR)/indirect_size_get.h \
	$(XORG_GLX_DIR)/indirect_table.c

######################################################################

GLAPI_XML_PATH := $(shell pkg-config --variable=datadir glapi)/xml/glapi

API_XML = $(wildcard $(GLAPI_XML_PATH)/*.xml)


COMMON = $(API_XML) gl_XML.py glX_XML.py license.py typeexpr.py

# Some of these xml files are not needed for ES, but there's not much
# harm in including them all as dependencies.
COMMON_ES = \
	$(COMMON) \
	$(API_XML)

COMMON_GLX = $(COMMON) glX_XML.py glX_proto_common.py

######################################################################

all: mesa xorg

mesa: $(MESA_OUTPUTS)

xorg: check-xorg-source $(XORG_OUTPUTS)

check-xorg-source:
	@if ! test -d $(XORG_GLX_DIR); then \
		echo "ERROR: Must specify path to xserver/ checkout. Set XORG_BASE env var."; \
		if test x$(XORG_BASE) != x; then \
			echo "'$(XORG_GLX_DIR)' does not exist."; \
		fi; \
		exit 1; \
	fi

clean:
	-rm -f *~ *.pyo
	-rm -f $(MESA_OUTPUTS)

######################################################################

$(XORG_GLAPI_DIR)/%.c: $(MESA_GLAPI_DIR)/%.c
	cp $< $@

$(XORG_GLAPI_DIR)/dispatch.h: $(MESA_DIR)/main/dispatch.h
	cp $< $@

$(XORG_GLAPI_DIR)/%.h: $(MESA_GLAPI_DIR)/%.h
	cp $< $@

######################################################################

$(MESA_GLAPI_DIR)/glapi_mapi_tmp.h: $(MESA_MAPI_DIR)/mapi_abi.py $(COMMON_ES)
	$(PYTHON2) $(PYTHON_FLAGS) $< \
		--printer glapi --mode lib $(GLAPI_XML_PATH)/gl_and_es_API.xml > $@

$(MESA_GLAPI_DIR)/glprocs.h: gl_procs.py $(COMMON)
	$(PYTHON2) $(PYTHON_FLAGS) $< -f $(GLAPI_XML_PATH)/gl_API.xml > $@

$(MESA_GLAPI_DIR)/glapitemp.h: gl_apitemp.py $(COMMON)
	$(PYTHON2) $(PYTHON_FLAGS) $< -f $(GLAPI_XML_PATH)/gl_API.xml > $@

$(MESA_GLAPI_DIR)/glapitable.h: gl_table.py $(COMMON)
	$(PYTHON2) $(PYTHON_FLAGS) $< -f $(GLAPI_XML_PATH)/gl_API.xml > $@

$(MESA_GLAPI_DIR)/glapi_gentable.c: gl_gentable.py $(COMMON)
	$(PYTHON2) $(PYTHON_FLAGS) $< -f $(GLAPI_XML_PATH)/gl_API.xml > $@

######################################################################

$(MESA_GLAPI_DIR)/glapi_x86.S: gl_x86_asm.py $(COMMON)
	$(PYTHON2) $(PYTHON_FLAGS) $< -f $(GLAPI_XML_PATH)/gl_API.xml > $@

$(MESA_GLAPI_DIR)/glapi_x86-64.S: gl_x86-64_asm.py $(COMMON)
	$(PYTHON2) $(PYTHON_FLAGS) $< -f $(GLAPI_XML_PATH)/gl_API.xml > $@

$(MESA_GLAPI_DIR)/glapi_sparc.S: gl_SPARC_asm.py $(COMMON)
	$(PYTHON2) $(PYTHON_FLAGS) $< -f $(GLAPI_XML_PATH)/gl_API.xml > $@

######################################################################

$(MESA_DIR)/main/enums.c: gl_enums.py $(COMMON_ES)
	$(PYTHON2) $(PYTHON_FLAGS) $< -f $(GLAPI_XML_PATH)/gl_and_es_API.xml > $@

$(MESA_DIR)/main/dispatch.h: gl_table.py $(COMMON)
	$(PYTHON2) $(PYTHON_FLAGS) $< -f $(GLAPI_XML_PATH)/gl_API.xml -m remap_table > $@

$(MESA_DIR)/main/remap_helper.h: remap_helper.py $(COMMON)
	$(PYTHON2) $(PYTHON_FLAGS) $< -f $(GLAPI_XML_PATH)/gl_API.xml > $@

######################################################################

$(MESA_GLX_DIR)/indirect.c: glX_proto_send.py $(COMMON_GLX)
	$(PYTHON2) $(PYTHON_FLAGS) $< -f $(GLAPI_XML_PATH)/gl_API.xml -m proto | $(INDENT) $(INDENT_FLAGS) > $@

$(MESA_GLX_DIR)/indirect.h: glX_proto_send.py $(COMMON_GLX)
	$(PYTHON2) $(PYTHON_FLAGS) $< -f $(GLAPI_XML_PATH)/gl_API.xml -m init_h > $@

$(MESA_GLX_DIR)/indirect_init.c: glX_proto_send.py $(COMMON_GLX)
	$(PYTHON2) $(PYTHON_FLAGS) $< -f $(GLAPI_XML_PATH)/gl_API.xml -m init_c > $@

$(MESA_GLX_DIR)/indirect_size.h $(XORG_GLX_DIR)/indirect_size.h: glX_proto_size.py $(COMMON_GLX)
	$(PYTHON2) $(PYTHON_FLAGS) $< -f $(GLAPI_XML_PATH)/gl_API.xml -m size_h --only-set -h _INDIRECT_SIZE_H_ \
	  | $(INDENT) $(INDENT_FLAGS) > $@

$(MESA_GLX_DIR)/indirect_size.c: glX_proto_size.py $(COMMON_GLX)
	$(PYTHON2) $(PYTHON_FLAGS) $< -f $(GLAPI_XML_PATH)/gl_API.xml -m size_c --only-set \
	  | $(INDENT) $(INDENT_FLAGS) > $@

######################################################################

$(XORG_GLX_DIR)/indirect_dispatch.c: glX_proto_recv.py $(COMMON_GLX)
	$(PYTHON2) $(PYTHON_FLAGS) $< -m dispatch_c > $@

$(XORG_GLX_DIR)/indirect_dispatch_swap.c: glX_proto_recv.py $(COMMON_GLX)
	$(PYTHON2) $(PYTHON_FLAGS) $< -m dispatch_c -s > $@

$(XORG_GLX_DIR)/indirect_dispatch.h: glX_proto_recv.py $(COMMON_GLX)
	$(PYTHON2) $(PYTHON_FLAGS) $< -m dispatch_h -f $(GLAPI_XML_PATH)/gl_and_glX_API.xml -s > $@

$(XORG_GLX_DIR)/indirect_size_get.h: glX_proto_size.py $(COMMON_GLX)
	$(PYTHON2) $(PYTHON_FLAGS) $< -m size_h --only-get -h '_INDIRECT_SIZE_GET_H_' \
	  | $(INDENT) $(INDENT_FLAGS) > $@

$(XORG_GLX_DIR)/indirect_size_get.c: glX_proto_size.py $(COMMON_GLX)
	$(PYTHON2) $(PYTHON_FLAGS) $< -m size_c | $(INDENT) $(INDENT_FLAGS) > $@

$(XORG_GLX_DIR)/indirect_reqsize.h: glX_proto_size.py $(COMMON_GLX)
	$(PYTHON2) $(PYTHON_FLAGS) $< -m reqsize_h --only-get -h '_INDIRECT_SIZE_GET_H_' \
	  | $(INDENT) $(INDENT_FLAGS) -l200 > $@

$(XORG_GLX_DIR)/indirect_reqsize.c: glX_proto_size.py $(COMMON_GLX)
	$(PYTHON2) $(PYTHON_FLAGS) $< -m reqsize_c | $(INDENT) $(INDENT_FLAGS) > $@

$(XORG_GLX_DIR)/indirect_table.c: glX_server_table.py $(COMMON_GLX)
	$(PYTHON2) $(PYTHON_FLAGS) $< -f $(GLAPI_XML_PATH)/gl_and_glX_API.xml > $@
