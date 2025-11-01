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

CC := $(if $(TOOLCHAIN_PREFIX),$(TOOLCHAIN_PREFIX)gcc,$(CC))

CFLAGS ?= -std=c11 -O2 -Wall -Wextra -Wpedantic
LDFLAGS ?=
LDLIBS ?= -pthread
TARGET := stage0_raw_socket_demo

all: $(TARGET)

$(TARGET): stage0_raw_socket_demo.c
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS) $(LDLIBS)

cross:
	$(MAKE) TOOLCHAIN_PREFIX=mips-rtl83xx-linux- all

clean:
	rm -f $(TARGET)

.PHONY: all clean cross
