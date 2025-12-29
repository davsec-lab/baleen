CONFIG_ROOT := ../Config

include $(CONFIG_ROOT)/makefile.config
include makefile.rules
include $(TOOLS_ROOT)/Config/makefile.default.rules

TOOL_CXXFLAGS += -Iinclude

vpath %.cpp src
vpath %.h include

TOOL_ROOTS := baleen

TOOL_TARGET := $(OBJDIR)baleen$(PINTOOL_SUFFIX)

BALEEN_MODULES := baleen \
                registry \
                language \
                allocation \
                extensions \
                utilities \
                object \
                logger

BALEEN_OBJS := $(addprefix $(OBJDIR), $(addsuffix $(OBJ_SUFFIX), $(BALEEN_MODULES)))

$(TOOL_TARGET): $(BALEEN_OBJS)
	$(LINKER) $(TOOL_LDFLAGS_NOOPT) $(LINK_EXE)$@ $^ $(TOOL_LPATHS) $(TOOL_LIBS)

all: $(TOOL_TARGET)