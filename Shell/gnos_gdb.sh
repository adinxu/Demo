#!/bin/bash

<<COMMENT
V1.0 编写完成
V1.1 调整bin_debug目录查找方式，以应对多个生成目录
V1.2 bin_debug目录已废弃，调整带符号表的进程的存放路径
#若是拷贝内容到脚本保存，需要注意linux和winodws换行符差异！！！
GNOS_GDB_SH_URL=https://yfgitlab.dahuatech.com/-/snippets/253/raw/main/init_gnos_gdb_env.sh?inline=false
curl -sSfk ${GNOS_GDB_SH_URL} | bash
COMMENT

TOOLS_SH_URL=https://yfgitlab.dahuatech.com/-/snippets/252/raw/main/tools.sh?inline=false
source <(curl -sSfk ${TOOLS_SH_URL})

#用于存放gdb脚本的位置
GDB_FILE_NAME=my-gdb
GDB_SCRIPT_FILE_NAME=gnos_env.gdb
#use gdb script rather than .gdbinit
#GDB_INIT_FILE_NAME=.gdbinit

#bin_debug PATH is not use now
#BIN_DEBUG_PATH=output/linux-arm64-charlie-sikadeer/nos/
DEBUG_PATH=build/linux-arm64-charlie-sikadeer/nos/platform/linux/bin/

WRONG_SRC_PATH=build/linux-arm64-charlie-sikadeer/nos
GDB_ORIGIN_FILE_NAME=output/debug-tools/aarch64-none-linux-gnu/bin/aarch64-none-linux-gnu-gdb
SHARE_ORIGIN_FILE_NAME=output/debug-tools/aarch64-none-linux-gnu/share/gdb

#use gdb script rather than .gdbinit
#restore_or_backup_file ~/.gdbinit
#echo "set auto-load safe-path /" >> ~/.gdbinit

if [ -f  "$GDB_ORIGIN_FILE_NAME" ] && [ -d  "$SHARE_ORIGIN_FILE_NAME" ]; then
	self_print "已确定gnos根目录"
else
	self_print "目录错误或未编译过，请放置于gnos根目录下执行，且保证存在output/debug-tools/aarch64-none-linux-gnu/bin/aarch64-none-linux-gnu-gdb文件"
	exit 1
fi


script_dir="$( pwd )"
self_print "当前脚本目录为$script_dir"

#bin_debug PATH is not use now
#BIN_DEBUG_PATH+=$(cd ${script_dir}/${BIN_DEBUG_PATH};find . -type d -name "bin_debug"|tail -n 1)
#self_print "bin_debug目录为$BIN_DEBUG_PATH"
self_print "debug目录为$DEBUG_PATH"

echo "#!/usr/bin/env bash
${script_dir}/${GDB_ORIGIN_FILE_NAME} -x ${script_dir}/${GDB_SCRIPT_FILE_NAME} --data-directory=${script_dir}/${SHARE_ORIGIN_FILE_NAME} "'$@'> ${GDB_FILE_NAME}

self_print "已创建gdb文件:${GDB_FILE_NAME}"

#添加执行权限
chmod +x ${GDB_FILE_NAME}

#创建gdb脚本:1.切换到debug交付件目录
#bin_debug PATH is not use now
#echo "cd ${script_dir}/${BIN_DEBUG_PATH}
echo "cd ${script_dir}/${DEBUG_PATH}
"> ${GDB_SCRIPT_FILE_NAME}

#2.增加源码路径
echo 'set directories $cdir:$cwd'":${script_dir}
">> ${GDB_SCRIPT_FILE_NAME}

#3.忽略信号
echo "handle SIGHUP noprint nostop
handle SIGCONT noprint nostop
handle SIGPIPE noprint nostop
handle SIGTSTP noprint nostop
handle SIGUSR2 noprint nostop
">> ${GDB_SCRIPT_FILE_NAME}

#4.便利性设置
echo "set pagination off
set print pretty
">> ${GDB_SCRIPT_FILE_NAME}
self_print "已创建gdb脚本:${GDB_SCRIPT_FILE_NAME}"
self_print "后续使用my-gdb进行调试即可
gdb脚本已切换到debug交付件目录，可在gdb内使用file命令直接选择要加载的进程，无需再手动指定目录
gdb脚本已切换源码路径，方便直接显示源码,或者直接使用tui
gdb脚本已忽略相关信号，防止断点外不必要的停止
gdb脚本已设置禁止分页，格式化打印等设置"
