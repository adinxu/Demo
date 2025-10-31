CC ?= gcc
CFLAGS ?= -std=c11 -O2 -Wall -Wextra -Wpedantic
LDFLAGS ?= 
LDLIBS ?= -pthread
TARGET := stage0_raw_socket_demo

all: $(TARGET)

$(TARGET): stage0_raw_socket_demo.c
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS) $(LDLIBS)

clean:
	rm -f $(TARGET)

.PHONY: all clean
