#!/bin/bash

readonly BASEDIR=$(readlink -f $(dirname $0))/../../..
source "$BASEDIR/script/common.sh"

CLI=$BASEDIR/lib/cxl_cli/build/cxl/cxl
ADDRESS=$1

if [ `whoami` != 'root' ]; then
	echo "This test requires root privileges"
	exit 2
fi

if [ $# -eq 0 ]; then
	echo  -e "\nScan media start address needed\n"
	echo  -e "Usage : $0 [address]"
	echo  -e "ex) $0 10000\n"
	exit 2
fi

if [ $ADDRESS -lt 1000 ]; then
	echo -e "\n[WARNING]"
	echo -e "Scan media start address should be greater than 0x1000."
	echo -e "Automatically setting address to 0x1000"
	ADDRESS=1000
fi

function background_warn(){
	echo ""
	echo "'$1' command could be executed in background"
	echo "Continue after background command is completed"
	read -s -n1 -p 'Press any key to continue...' keypress
	echo ""
	echo ""
}

# Scan Media cmds
log_normal "[get-scan-media-caps]"
echo "$ cxl get-scan-media-caps mem0 -a 0x$ADDRESS -l 0x80"
$CLI get-scan-media-caps mem0 -a 0x$ADDRESS -l 0x80

log_normal "[scan-media]"
echo "$ cxl scan-media mem0 -a 0x$ADDRESS -l 0x80"
$CLI scan-media mem0 -a 0x$ADDRESS -l 0x80

background_warn "scan-media"

log_normal "[get-scan-media]"
echo "$ cxl get-scan-media mem0"
$CLI get-scan-media mem0

# Sanitize cmd
log_normal "[sanitize-memdev]"
echo "$ cxl sanitize-memdev mem0"
$CLI sanitize-memdev mem0

background_warn "sanitize-memdev"

exit 0
