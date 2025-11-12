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
CXXFLAGS ?= -std=gnu++11 -O2 -Wall -Wextra -Wpedantic
CXXFLAGS += -Iinclude -pthread
LDFLAGS ?=
LDLIBS ?=

CSRCS := \
	common/td_logging.c \
	common/td_config.c \
	common/terminal_manager.c \
	common/terminal_netlink.c \
	adapter/adapter_registry.c \
	adapter/realtek_adapter.c \
	stub/td_switch_mac_stub.c \
	main/terminal_main.c

CPPSRCS := \
	common/terminal_northbound.cpp

OBJS := $(CSRCS:.c=.o) $(CPPSRCS:.cpp=.o)
TARGET := terminal_discovery
TEST_TARGET := terminal_discovery_tests
TEST_SRCS := tests/terminal_manager_tests.c
TEST_OBJS := $(TEST_SRCS:.c=.o)
TEST_DEPS := common/terminal_manager.o common/td_logging.o
INTEGRATION_TEST_TARGET := terminal_integration_tests
INTEGRATION_TEST_SRCS := tests/terminal_integration_tests.cpp
INTEGRATION_TEST_OBJS := $(INTEGRATION_TEST_SRCS:.cpp=.o)
INTEGRATION_TEST_DEPS := common/terminal_manager.o common/td_logging.o common/terminal_northbound.o

STUB_TEST_TARGET := td_switch_mac_stub_tests
STUB_TEST_SRCS := tests/td_switch_mac_stub_tests.c
STUB_TEST_OBJS := $(STUB_TEST_SRCS:.c=.o)
STUB_TEST_DEPS := stub/td_switch_mac_stub.o

EMBED_TEST_TARGET := terminal_embedded_init_tests
EMBED_TEST_SRCS := tests/terminal_embedded_init_tests.c
EMBED_TEST_OBJS := $(EMBED_TEST_SRCS:.c=.o) tests/terminal_main_for_tests.o
EMBED_TEST_DEPS := common/td_config.o common/td_logging.o

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(TEST_TARGET): $(TEST_OBJS) $(TEST_DEPS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(INTEGRATION_TEST_TARGET): $(INTEGRATION_TEST_OBJS) $(INTEGRATION_TEST_DEPS)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(STUB_TEST_TARGET): $(STUB_TEST_OBJS) $(STUB_TEST_DEPS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(EMBED_TEST_TARGET): $(EMBED_TEST_OBJS) $(EMBED_TEST_DEPS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)

tests/terminal_main_for_tests.o: main/terminal_main.c
	$(CC) $(CFLAGS) -DTD_DISABLE_APP_MAIN -c $< -o $@

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(TARGET) $(OBJS) \
		$(TEST_TARGET) $(TEST_OBJS) \
		$(INTEGRATION_TEST_TARGET) $(INTEGRATION_TEST_OBJS) \
		$(STUB_TEST_TARGET) $(STUB_TEST_OBJS) \
		$(EMBED_TEST_TARGET) $(EMBED_TEST_OBJS)

cross:
	$(MAKE) TOOLCHAIN_PREFIX=mips-rtl83xx-linux- all

cross-generic:
	$(MAKE) TOOLCHAIN_PREFIX=mips-linux-gnu- all


.PHONY: all clean cross cross-generic test

test: $(TEST_TARGET) $(INTEGRATION_TEST_TARGET) $(STUB_TEST_TARGET) $(EMBED_TEST_TARGET)
	./$(TEST_TARGET)
	./$(INTEGRATION_TEST_TARGET)
	./$(STUB_TEST_TARGET)
	./$(EMBED_TEST_TARGET)
