TARGET = l2packet
SRC = main.c

# 默认本地编译
CC ?= gcc
CFLAGS ?= -Wall -O2
LDFLAGS ?= -lpcap -lpthread

# 支持外部附加额外的CFLAGS和LDFLAGS
CFLAGS += $(EXTRA_CFLAGS)
LDFLAGS += $(EXTRA_LDFLAGS)

# 交叉编译支持
# 用法示例:
#   make ARCH=arm64 CROSS_COMPILE=aarch64-none-linux-gnu-
#   make ARCH=mips CROSS_COMPILE=mips-linux-gnu-
ifeq ($(ARCH),arm64)
	CC := $(CROSS_COMPILE)gcc
endif
ifeq ($(ARCH),mips)
	CC := $(CROSS_COMPILE)gcc
endif

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

clean:
	rm -f $(TARGET)

help:
	@echo "用法: make [目标] [变量=值]"
	@echo ""
	@echo "常用目标:"
	@echo "  all      构建程序 (默认)"
	@echo "  clean    删除生成的可执行文件"
	@echo "  help     显示本帮助信息"
	@echo ""
	@echo "交叉编译示例:"
	@echo "  make ARCH=arm64 CROSS_COMPILE=aarch64-none-linux-gnu-"
	@echo "  make ARCH=mips CROSS_COMPILE=mips-linux-gnu-"
	@echo ""
	@echo "变量说明:"
	@echo "  ARCH           指定目标架构 (如 arm64, mips)"
	@echo "  CROSS_COMPILE  指定交叉编译工具链前缀 (如 aarch64-linux-gnu-)"
	@echo ""

.PHONY: all clean help
