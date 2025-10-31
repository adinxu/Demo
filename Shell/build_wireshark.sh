#!/bin/bash

<<COMMENT
SCRIPT_SH_URL=https://yfgitlab.dahuatech.com/-/snippets/256/raw/main/build_wireshark.sh?inline=false
GENERATE_SCRIPT_SH_URL=https://yfgitlab.dahuatech.com/-/snippets/255/raw/main/generate_script.sh?inline=false
source <(curl -sSfk ${GENERATE_SCRIPT_SH_URL})
generate_new_script ${SCRIPT_SH_URL}
COMMENT


<<COMMENT
print packet use this function,but this func only can print max 256 bytes data:

int g_bPrintPacket = 1;
void printBufferHex(const unsigned char *buffer, int buffer_size)
{
	int i = 0;
	int count = 0;
	char buf[543] = {0};  /* 256 bytes * 2  + 31 spaces*/
	
	if(!g_bPrintPacket)
	{
		return;
	}

	count += snprintf(buf + count, (sizeof(buf) - count), "Raw:");
	for (i = 0 ; i < buffer_size; ++i) {
		if (count > sizeof buf) break;

		if (!(i % 16)) {
			/* The syslog-ng we used not permit multi-line log msg, every
			 * newline character present a split new log msg */
			count += snprintf(buf + count, (sizeof(buf) - count), " %02hhx", buffer[i]);
		} else {
			count += snprintf(buf + count, (sizeof(buf) - count), "%02hhx", buffer[i]);
		}
	}
	printf("%s", buf);
}
COMMENT

function gen_packet() {
	input=$1
	echo ${input}|
	egrep "(\s[0-9a-f]{32}\b)+(\s[0-9a-f]+)*" -o|
	egrep -o "\S.+\S"|
	awk 'BEGIN { OFS="\n" }{for(i = 1 ; i <= NF ; i++) print $i}'|
	awk -F '' 'BEGIN { OFS="" 
	ORS=""}{
	printf("%03x0 ",NR-1)
	for(i=1;i<=NF;i++)
	{
	if(i%2==1&&i!=1)
	{
	printf(" ")
	}
	print $i
	}
	printf("\n")
	}' >> ${wFile}
}

function build_wireshark() {
	if [ -f "${rFile}" ]; then
		cat ${rFile}|
		egrep "Raw:\s"|
		while read line
		do
			self_print "filter ${line}"
			gen_packet "${line}"
		done
		return 0
	else
		self_print "cant find file ${rFile}!!!"
		exit 1
	fi
}

wFile=./rawpacket
if [ -f "${wFile}" ]; then
	cat /dev/null>${wFile}
fi

if [ -n "$1" ]; then
	rFile=$1
else
	rFile=./rawdata
fi
build_wireshark
echo "process over,result has been written to file ${wFile}"
cat ${wFile}
