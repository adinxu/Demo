ROOT := $(abspath ../..)
SRC_DIR := $(ROOT)/src
COMMON_INC_DIR := $(ROOT)/src/commonlib/include
OUT_DIR := $(ROOT)/out/common
OBJ_DIR := $(OUT_DIR)/obj
LIB_DIR := $(OUT_DIR)/lib

CC ?= gcc
CXX ?= g++
AR ?= ar

CFLAGS ?= -std=c11 -O2 -Wall -Wextra -Wpedantic
CFLAGS += -I$(COMMON_INC_DIR) -pthread
CXXFLAGS ?= -std=gnu++11 -O2 -Wall -Wextra -Wpedantic
CXXFLAGS += -I$(COMMON_INC_DIR) -pthread
LDFLAGS ?=
LDLIBS ?=
LDLIBS += -pthread

COMMON_C_SRCS := \
	commonlib/src/td_logging.c \
	commonlib/src/td_config.c \
	commonlib/src/terminal_manager.c \
	commonlib/src/terminal_netlink.c
COMMON_CPP_SRCS := \
	commonlib/src/terminal_northbound.cpp
COMMON_C_OBJS := $(COMMON_C_SRCS:%.c=$(OBJ_DIR)/%.o)
COMMON_CPP_OBJS := $(COMMON_CPP_SRCS:%.cpp=$(OBJ_DIR)/%.o)
COMMON_OBJS := $(COMMON_C_OBJS) $(COMMON_CPP_OBJS)
COMMON_LIB := $(LIB_DIR)/libtd_common.a

DIRS := $(OBJ_DIR) $(LIB_DIR)

.PHONY: all clean

all: $(COMMON_LIB)

$(COMMON_LIB): $(COMMON_OBJS) | $(LIB_DIR)
	$(AR) rcs $@ $^

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c | $(OBJ_DIR)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.cpp | $(OBJ_DIR)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(DIRS):
	@mkdir -p $@

clean:
	rm -rf $(OUT_DIR)
