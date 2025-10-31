#!/bin/bash

<<COMMENT
V0.1 初稿完成
V0.2 改写为更简洁的形式，调试文件和gdb脚本同步生成
v0.3 优化脚本，增加对动态库的处理
#若是拷贝内容到脚本保存，需要注意linux和winodws换行符差异！！！
SCRIPT_SH_URL=https://yfgitlab.dahuatech.com/-/snippets/338/raw/main/autosave_debuginfo.sh?inline=false
GENERATE_SCRIPT_SH_URL=https://yfgitlab.dahuatech.com/-/snippets/255/raw/main/generate_script.sh?inline=false
source <(curl -sSfk ${GENERATE_SCRIPT_SH_URL})
generate_new_script ${SCRIPT_SH_URL}
COMMENT

#基本原理:1.linux中使用aarch64-none-linux-gnu-objcopy的--only-keep-debug选项可以将进程文件或动态库文件的调试信息单独保存到一个调试文件，以用于后续调试。
#2.在gdb中可以使用source命令来加载一个gdb脚本文件，这个脚本文件中可以包含一系列的gdb指令，用于调试对应的进程。
#3.在gdb中可以使用add-symbol-file来添加调试文件用来调试，该命令接受两个参数，第一个参数为调试文件，第二个参数为加载地址， 在使用时指定的地址为对进程文件或动态库使用readelf -S的输出信息中.text段的Address字段值(位于第4列)，
#如果是动态库，还要特殊处理，即第二个参数为readelf -S的输出信息中.text段的Address字段值加上进程实际运行时动态库的加载地址，这个加载地址可以通过gdb中info proc mappings命令的起始地址获取
#4.在gdb中可以使用set confirm off来关闭gdb的确认提示，以便在脚本中自动确认所有提示，可以用此命令来避免在gdb脚本中的使用add-symbol-file时的确认提示
#5.在gdb中可以使用set logging命令可以来将gdb的输出信息保存到一个文件中，以便后续处理
#目标:结合上述基本原理中的几个原理，生成一个shell脚本，这个脚本可以指定两个参数，分别为进程目录及输出目录，脚本处理时，会先清空输出目录中旧的调试文件和gdb脚本文件，然后在脚本参数指定的进程目录下遍历的所有进程文件及动态库文件，
#生成每个进程及动态库对应的调试文件及gdb脚本文件到由脚本参数指定的输出目录下，其中调试文件命名为{进程名或动态库名.dbg}，gdb脚本文件命名为{进程名或动态库名.gdb}。


#参数检查
if [ $# -ne 2 ]; then
    echo "Usage: $0 <process_dir> <output_dir>"
    exit 1
fi

#参数赋值
process_dir=$1
output_dir=$2

#防止输出目录不存在，创建输出目录
mkdir -p $output_dir

#清空输出目录，匹配所有的调试文件和gdb脚本文件
rm -f $output_dir/*.dbg
rm -f $output_dir/*.gdb
#输出清空信息
echo "Clear $output_dir success"

#遍历目录下的所有文件
for file in $process_dir/*
do
    #非可执行文件或非ELF文件跳过不处理
    if [ ! -f $file ] || [ "$(file $file | grep ELF)" == "" ]; then
        continue
    fi
    #获取文件名
    process_name=$(basename $file)
    #生成调试文件名
    dbg_file=$output_dir/$process_name.dbg
    #生成gdb脚本文件名
    gdb_file=$output_dir/$process_name.gdb

    #生成调试文件
    aarch64-none-linux-gnu-objcopy --only-keep-debug $file $dbg_file

    #生成gdb脚本文件
    echo "set confirm off" > $gdb_file
    #区分进程文件和动态库文件的处理
    #动态库文件处理，使用readelf -h的输出信息中的Type字段值为DYN来判断
    if [ "$(readelf -h $file | grep 'Type' | awk '{print $2}')" == "DYN" ]; then
        #处理逻辑：1.获取.text段的Address字段值备用 2.在gdb脚本中关闭分页，设置log打开，然后指定log文件目录到/tmp下，将info proc mappings的输出信息保存到log文件中，然后设置log关闭
        #3.在gdb脚本中调用shell命令从保存的文件中获取dynamic库的加载地址,并写入到/tmp下的一个新的gdb脚本文件中保存为变量
        #4.在gdb脚本文件中使用source命令加载新的gdb脚本文件，以便获取设置的变量
        #5.在gdb脚本中使用add-symbol-file命令加载调试文件,加载地址为.text段的Address字段值加上动态库的加载地址，直接用加号连接两个地址即可
        #获取.text段的Address字段值
        text_addr=$(readelf -S $file | grep '\.text' | awk '{print $4}')
        #关闭分页
        echo "set pagination off" >> $gdb_file
        #设置log打开
        echo "set logging on" >> $gdb_file
        #设置log文件目录到/tmp下
        echo "set logging file /tmp/mappings.log" >> $gdb_file
        #获取动态库的加载地址
        echo "info proc mappings" >> $gdb_file
        #设置log关闭
        echo "set logging off" >> $gdb_file
        #创建一个gdb变量，其值为从保存的文件中使用shell命令获取的dynamic库的加载地址，其中动态库名字可能有多行，只取第一行的第一列
        echo "shell echo 'set \$dyn_addr = 0x$(cat /tmp/mappings.log | grep $(basename $file) | head -n 1 | awk '{print $1}');' > /tmp/dyn_addr.gdb" >> $gdb_file
        #加载新的gdb脚本文件
        echo "source /tmp/dyn_addr.gdb" >> $gdb_file
        #加载调试文件，直接用加号连接两个地址即可，不需要真正计算
        echo "add-symbol-file $(basename $dbg_file) 0x\$dyn_addr + 0x$text_addr" >> $gdb_file
    else
        #进程文件处理
        echo "add-symbol-file $(basename $dbg_file) 0x$(readelf -S $file | grep '\.text' | awk '{print $4}')" >> $gdb_file
    fi

    #输出调试文件和gdb脚本文件生成信息
    echo "Generate dbg file: $dbg_file and gdb file: $gdb_file for $file success"

done
