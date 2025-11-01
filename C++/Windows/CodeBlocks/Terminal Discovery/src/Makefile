CC ?= gcc
AR ?= ar
CROSS ?=

ifeq ($(CROSS),)
TOOLCHAIN_PREFIX ?=
else
TOOLCHAIN_PREFIX := $(CROSS)
endif

ifeq ($(TOOLCHAIN_PREFIX),)
TOOLCHAIN_PREFIX ?=
endif

CC := $(if $(TOOLCHAIN_PREFIX),$(TOOLCHAIN_PREFIX)gcc,$(CC))
AR := $(if $(TOOLCHAIN_PREFIX),$(TOOLCHAIN_PREFIX)ar,$(AR))

CFLAGS ?= -std=c11 -O2 -Wall -Wextra -Wpedantic
CFLAGS += -Iinclude -pthread
LDFLAGS ?=
LDLIBS ?=

SRCS := \
	common/td_logging.c \
	common/td_config.c \
	common/terminal_manager.c \
	adapter/adapter_registry.c \
	adapter/realtek_adapter.c

OBJS := $(SRCS:.c=.o)
TARGET := libterminal_discovery.a

all: $(TARGET)

$(TARGET): $(OBJS)
	$(AR) rcs $@ $^

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(TARGET) $(OBJS)

cross:
	$(MAKE) TOOLCHAIN_PREFIX=mips-rtl83xx-linux- all

cross-generic:
	$(MAKE) TOOLCHAIN_PREFIX=mips-linux-gnu- all

.PHONY: all clean cross cross-generic
