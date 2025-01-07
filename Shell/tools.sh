#!/bin/bash

<<COMMENT
#this is a tool scrpt,use this script just like this:
TOOLS_SH_URL=https://yfgitlab.dahuatech.com/-/snippets/252/raw/main/tools.sh?inline=false
source <(curl -sSfk ${TOOLS_SH_URL})

curl -o tools.sh -k ${TOOLS_SH_URL} && chmod +x tools.sh && source ./tools.sh

curl -sSfk ${TOOLS_SH_URL} >> xxx.sh

curl -sSfk ${TOOLS_SH_URL} | bash
COMMENT

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

function is_absolute_path() {
	tfile=$1
	if [ -n "${tfile}" ]&&[[ "${tfile}" =~ ^/.* ]]; then
		return 1
	else
		echo "Error: The input argument ${tfile} must be a absolute path."
		return 0
	fi
}
function get_script_save_path() {
	basepath=$(cd `dirname $0`; pwd)
	self_print "script path is ${basepath}"
	return ${basepath}
}

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
		#ÏÈ±¸·Ý
		cp -n ${file_full_path} ${file_full_path}.bak
		self_print "backup ${file_full_path}>${file_full_path}.bak"
	fi
}

function force_cp() {
	src_target=$1
	dst_target=$2
	if [ -n "${src_target}" ]&&[ -n "${dst_target}" ]&&[ -e "${src_target}" ]; then
		/bin/cp -rf ${src_target} ${dst_target}
		self_print "cp ${src_target}>${dst_target}"
	fi
}

function excute_script_right_now() {
	script_file=$1
	chmod +x ${script_file}
	source ${script_file}
	self_print "excute script ${script_file} success!"
}

function check_parent_path_before_delete() {
	ppath_of_dtarget=$1
	dtarget_path=$2
	if [ -n "${ppath_of_dtarget}" ]&&[ -n "${dtarget_path}" ]&&[ -e "${ppath_of_dtarget}/${dtarget_path}" ]; then
		self_print "excute rm -rf to ${ppath_of_dtarget}/${dtarget_path}"
		rm -rf "${ppath_of_dtarget}/${dtarget_path}"
	fi	
}
