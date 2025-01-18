#!/bin/bash

<<COMMENT
V1.0 编写完成
V1.1 添加注释说明
#若是拷贝内容到脚本保存，需要注意linux和winodws换行符差异！！！
#this is a tool scrpt,use this script just like this:
# Download the tools.sh file from the specified URL and execute it as a subshell
TOOLS_SH_URL=https://yfgitlab.dahuatech.com/-/snippets/252/raw/main/tools.sh?inline=false
source <(curl -sSfk ${TOOLS_SH_URL})

# Download the tools.sh file from the specified URL, make it executable, and execute it
curl -o tools.sh -k ${TOOLS_SH_URL} && chmod +x tools.sh && source ./tools.sh

# Append the contents of the tools.sh file from the specified URL to the xxx.sh file
curl -sSfk ${TOOLS_SH_URL} >> xxx.sh

# Download the tools.sh file from the specified URL and execute it as a bash script
curl -sSfk ${TOOLS_SH_URL} | bash
COMMENT

# Function to print a string surrounded by a border
#
# Usage: self_print <string>
#   <string> - The string to be printed
#
# This function takes a string as input and prints it surrounded by a border of equal signs.
# If the input string is empty, nothing will be printed.
function self_print() {
	pstr=$1
	if [ -n "${pstr}" ]; then
		cat << EOF
====================
${pstr}
====================
EOF
	fi
}

# Function: is_absolute_path
# Description: Checks if a given path is an absolute path.
# Parameters:
#   - tfile: The path to be checked.
# Returns:
#   - 1 if the path is absolute.
#   - 0 if the path is not absolute.
function is_absolute_path() {
	tfile=$1
	if [ -n "${tfile}" ]&&[[ "${tfile}" =~ ^/.* ]]; then
		return 1
	else
		echo "Error: The input argument ${tfile} must be an absolute path."
		return 0
	fi
}

# Function: get_script_save_path
# Description: Gets the absolute path of the script file.
# Returns:
#   - The absolute path of the script file.
function get_script_save_path() {
	basepath=$(cd `dirname $0`; pwd)
	self_print "script path is ${basepath}"
	return ${basepath}
}

# Function: restore_or_backup_file
# Description: Restores or backs up a file.
# Parameters:
#   - file_full_path: The full path of the file to be restored or backed up.
function restore_or_backup_file() {
	file_full_path=$1
	if [ -f ${file_full_path}.bak ]; then
		cat ${file_full_path}.bak >  ${file_full_path}
		self_print "restore ${file_full_path}.bak>${file_full_path}"
	elif [ ! -f ${file_full_path} ]; then
		touch ${file_full_path}
		touch ${file_full_path}.bak
		self_print "touch file ${file_full_path} and ${file_full_path}.bak"
	else
		cp -n ${file_full_path} ${file_full_path}.bak
		self_print "backup ${file_full_path}>${file_full_path}.bak"
	fi
}

# Function: force_cp
# Description: Copies a file or directory forcefully.
# Parameters:
#   - src_target: The source file or directory to be copied.
#   - dst_target: The destination file or directory.
function force_cp() {
	src_target=$1
	dst_target=$2
	if [ -n "${src_target}" ]&&[ -n "${dst_target}" ]&&[ -e "${src_target}" ]; then
		/bin/cp -rf ${src_target} ${dst_target}
		self_print "cp ${src_target}>${dst_target}"
	fi
}

# Function: excute_script_right_now
# Description: Executes a script file immediately.
# Parameters:
#   - script_file: The script file to be executed.
function excute_script_right_now() {
	script_file=$1
	chmod +x ${script_file}
	source ${script_file}
	self_print "execute script ${script_file} success!"
}

# Function: check_parent_path_before_delete
# Description: Checks the parent path before deleting a target path.
# Parameters:
#   - ppath_of_dtarget: The parent path of the target path.
#   - dtarget_path: The target path to be deleted.
function check_parent_path_before_delete() {
	ppath_of_dtarget=$1
	dtarget_path=$2
	if [ -n "${ppath_of_dtarget}" ]&&[ -n "${dtarget_path}" ]&&[ -e "${ppath_of_dtarget}/${dtarget_path}" ]; then
		self_print "execute rm -rf to ${ppath_of_dtarget}/${dtarget_path}"
		rm -rf "${ppath_of_dtarget}/${dtarget_path}"
	fi	
}
