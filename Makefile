# global
TARGET   := pcc
BLD_DIR  := output
INC_DIR  := include
SRC_DIR  := source
BIN_DIR  := bin
TOOL_DIR := tool
# source codes
SRCS     := $(shell find $(SRC_DIR) -name *.c)
OBJS     := $(SRCS:%=$(BLD_DIR)/%.o)
DEPS     := $(OBJS:.o=.d)
# tools
TSRCS    := $(shell find $(TOOL_DIR) -name *.c)
TOBJS    := $(filter-out $(BLD_DIR)/$(SRC_DIR)/main.c.o,$(OBJS))
TDEPS    := $(TOBJS:.o=.d)
TOOLS    := $(TSRCS:$(TOOL_DIR)/%.c=$(BIN_DIR)/%)
# build config
CC       := cc
DEBUG    := -g
CCFLAGS  := -I$(INC_DIR) $(DEBUG) -MMD -MP -std=gnu99
LDFLAGS  := -I$(INC_DIR) $(DEBUG)

# all build targets
all: $(TARGET) $(TOOLS)
	@cp $(TARGET) bin

# target
$(TARGET): $(OBJS)
	$(CC) $(LDFLAGS) $^ -o $@

# c source
$(BLD_DIR)/%.c.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CCFLAGS) -c $< -o $@
# tools
$(BIN_DIR)/%: $(TOBJS) $(BLD_DIR)/$(TOOL_DIR)/%.c.o
	$(CC) $(LDFLAGS) $^ -o $@

# include dependencies
-include $(DEPS)

setup: clean index
	bear -- make

format:
	find $(INC_DIR) $(SRC_DIR) -type f | xargs -I {} clang-format -i {}

index: clean
	find $(INC_DIR) $(SRC_DIR) -type f | sort > cscope.files
	-cscope -bqk
	-ctags -e -L cscope.files

clean:
	@rm -rf $(BLD_DIR) $(TARGET) bin/* viz* 2>/dev/null
	@find . -iname "dag*.dot" -or -iname "dag*.dot.pdf" | xargs -I {} rm -f {}
	@find . -iname "*.o" -or -iname "*.run" -or -iname "core*" | xargs -I {} rm -f {}
	@find test -iname "*.s" -or -iname "*.o" | xargs -I {} rm -f {}
	@find example -iname "*.s" -or -iname "*.o" | xargs -I {} rm -f {}

distclean:
	$(RM) -r compile_commands.json cscope.* TAGS

.PHONY: all clean distclean index setup
