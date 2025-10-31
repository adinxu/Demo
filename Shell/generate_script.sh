#!/bin/bash

<<COMMENT
#this is a script to generate a new shell script with tools.sh,just use it like this:
SCRIPT_SH_URL=
GENERATE_SCRIPT_SH_URL=https://yfgitlab.dahuatech.com/-/snippets/255/raw/main/generate_script.sh?inline=false
source <(curl -sSfk ${GENERATE_SCRIPT_SH_URL})
generate_new_script ${SCRIPT_SH_URL}
COMMENT

function generate_new_script() {
	
	if [ -z "$1" ]; then
		echo "Error: The input argument is empty"
		return 1
	fi
	
	SCRIPT_SH_URL=$1
	SCRIPT_NAME="$(echo "${SCRIPT_SH_URL}"|grep -P -o "(?<=\/raw\/main\/).+?\.sh")"
	if [ -z "$SCRIPT_NAME" ]; then
		echo "Error: The input argument not a correct URL"
		return 2
	fi
	TOOLS_SH_URL=https://yfgitlab.dahuatech.com/-/snippets/252/raw/main/tools.sh?inline=false

	source <(curl -sSfk ${TOOLS_SH_URL})
	self_print "prepare to generate script ${SCRIPT_NAME}"
	echo "#!/bin/bash" > ${SCRIPT_NAME}
	echo "<<COMMENT"   >> ${SCRIPT_NAME}
	echo "#This is a auto-generated script for ${SCRIPT_SH_URL}" >> ${SCRIPT_NAME}
	echo "#If you want to update the script, simply execute the cmd command below:" >> ${SCRIPT_NAME}
	echo "SCRIPT_SH_URL=${SCRIPT_SH_URL}" >> ${SCRIPT_NAME}
	echo "GENERATE_SCRIPT_SH_URL=https://yfgitlab.dahuatech.com/-/snippets/255/raw/main/generate_script.sh?inline=false" >> ${SCRIPT_NAME}
	echo 'source <(curl -sSfk ${GENERATE_SCRIPT_SH_URL})' >> ${SCRIPT_NAME}
	echo 'generate_new_script ${SCRIPT_SH_URL}'>> ${SCRIPT_NAME}
	echo "COMMENT" >> ${SCRIPT_NAME}

	curl -sSfk ${TOOLS_SH_URL}  >> ${SCRIPT_NAME}
	self_print "processing ${TOOLS_SH_URL}"
	curl -sSfk ${SCRIPT_SH_URL} >> ${SCRIPT_NAME}
	self_print "processing ${SCRIPT_SH_URL}"
	chmod +x ${SCRIPT_NAME}
	self_print "process over,now you can use ${SCRIPT_NAME} now"
	self_print "如有bug，请联系徐伟东(344272)解决"
}

