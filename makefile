CONFIG_ROOT := ../Config

include $(CONFIG_ROOT)/makefile.config
include makefile.rules
include $(TOOLS_ROOT)/Config/makefile.default.rules

TOOL_ROOTS := baleen

all: obj-intel64/baleen.so
