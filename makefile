CONFIG_ROOT := ../Config

include $(CONFIG_ROOT)/makefile.config
include makefile.rules
include $(TOOLS_ROOT)/Config/makefile.default.rules

TOOL_ROOTS := baleen

# Link registry.o with baleen
$(OBJDIR)baleen$(PINTOOL_SUFFIX): $(OBJDIR)baleen$(OBJ_SUFFIX) $(OBJDIR)registry$(OBJ_SUFFIX) $(OBJDIR)language$(OBJ_SUFFIX)
	$(LINKER) $(TOOL_LDFLAGS_NOOPT) $(LINK_EXE)$@ $^ $(TOOL_LPATHS) $(TOOL_LIBS)

all: obj-intel64/baleen.so