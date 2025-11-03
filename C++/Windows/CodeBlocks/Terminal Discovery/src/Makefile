CXX ?= g++
CC ?= gcc
CROSS ?=

ifeq ($(CROSS),)
TOOLCHAIN_PREFIX ?=
else
TOOLCHAIN_PREFIX := $(CROSS)
endif

ifeq ($(TOOLCHAIN_PREFIX),)
TOOLCHAIN_PREFIX ?=
endif

CXX := $(if $(TOOLCHAIN_PREFIX),$(TOOLCHAIN_PREFIX)g++,$(CXX))
CC := $(if $(TOOLCHAIN_PREFIX),$(TOOLCHAIN_PREFIX)gcc,$(CC))

CFLAGS ?= -std=c11 -O2 -Wall -Wextra -Wpedantic
CFLAGS += -Iinclude -pthread
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -Wpedantic
CXXFLAGS += -Iinclude -pthread
LDFLAGS ?=
LDLIBS ?=

CSRCS := \
	common/td_logging.c \
	common/td_config.c \
	common/terminal_manager.c \
	adapter/adapter_registry.c \
	adapter/realtek_adapter.c \
	main/terminal_main.c

CPPSRCS := \
	common/terminal_northbound.cpp

OBJS := $(CSRCS:.c=.o) $(CPPSRCS:.cpp=.o)
TARGET := terminal_discovery

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(TARGET) $(OBJS)

cross:
	$(MAKE) TOOLCHAIN_PREFIX=mips-rtl83xx-linux- all

cross-generic:
	$(MAKE) TOOLCHAIN_PREFIX=mips-linux-gnu- all

.PHONY: all clean cross cross-generic
