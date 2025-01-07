#!/bin/bash
#基本原理:1.linux中使用aarch64-none-linux-gnu-objcopy的--only-keep-debug选项可以将进程文件的调试信息单独保存到一个调试文件，以用于后续调试
#2.在gdb中可以使用add-symbol-file来添加调试文件用来调试，在使用时指定的地址为对进程文件使用readelf -S的输出信息中.text段的Address字段值
#目标:结合上述基本原理中的原理1和原理2，生成一个shell脚本，这个脚本可以在脚本参数指定的路径下遍历的所有进程文件，应用下面提到的单个进程处理逻辑，然后生成一系列调试文件到由脚本参数指定的另一路径下，
#这些调试文件的命名是由{对应进程的名字_add-symbol-file_Address字段值.dbg}组成的，生成这些调试文件后，就可以在gdb中自定义gdb函数，
#从调试文件名中提取出对应进程的名字和加载指令，以应用调试文件，最后组成的指令为{add-symbol-file 调试文件名 加载指令}
#单个进程处理逻辑:
#1.获取进程名字
#2.使用readelf -S命令获取进程的.text段的Address字段值,将字段值前面附加0x，以便后续使用
#3.将第1步中的进程名字，指令add-symbol-file，以及第2步中的Address字段值拼接，组成要生成的调试文件的文件名，例如进程名字为test，Address字段值为0x1000，则要生成的调试文件的文件名为{test_add-symbol-file_0x1000.dbg}
#4.使用aarch64-none-linux-gnu-objcopy的--only-keep-debug选项将调试信息保存到调试文件中，命名为第3步中的文件名

# 检查参数个数
if [ $# -ne 2 ]; then
    echo "Usage: $0 <process_files_path> <debug_files_path>"
    exit 1
fi

PROCESS_PATH=$1
DEBUG_PATH=$3

# 确保目标目录存在
mkdir -p "$DEBUG_PATH"

#匹配调试文件模式删除之前生成的调试文件
rm -f "$DEBUG_PATH"/*_add-symbol-file_*.dbg

# 遍历进程文件目录下的所有文件
find "$PROCESS_PATH" -type f -executable | while read process_file; do
    # 检查是否为ELF文件
    if file "$process_file" | grep -q "ELF"; then
        # 获取进程名
        process_name=$(basename "$process_file")
        
        # 获取.text段的Address值
        text_addr=$(readelf -S "$process_file" | grep "\.text" | awk '{print "0x"$4}')
        
        if [ ! -z "$text_addr" ]; then
            # 构建调试文件名
            debug_file="${DEBUG_PATH}/${process_name}_add-symbol-file_${text_addr}.dbg"
            
            # 使用aarch64-none-linux-gnu-objcopy提取调试信息
            aarch64-none-linux-gnu-objcopy --only-keep-debug "$process_file" "$debug_file"
            
            if [ $? -eq 0 ]; then
                echo "Successfully created debug file: $debug_file"
            else
                echo "Failed to create debug file for: $process_file"
            fi
        else
            echo "Could not find .text section address for: $process_file"
        fi
    fi
done
