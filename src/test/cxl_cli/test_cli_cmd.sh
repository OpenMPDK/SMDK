#!/bin/bash

readonly BASEDIR=$(readlink -f $(dirname $0))/../../..
source "$BASEDIR/script/common.sh"

CLI=$BASEDIR/lib/cxl_cli/build/cxl/cxl
ADDRESS=$1
if [ -z $RUN_ON_QEMU ]; then
	DAX_ACCESS_PGM=$BASEDIR/src/test/cxl_cli/devmem2
fi

if [ `whoami` != 'root' ]; then
	echo "This test requires root privileges"
	exit 2
fi

if [ $# -eq 0 ]; then
	echo  -e "\nPoison address needed\n"
	echo  -e "Usage : $0 [address]"
	echo  -e "ex) $0 10000\n"
	exit 2
fi

if [ $ADDRESS -lt 1000 ]; then
	echo -e "\n[WARNING]"
	echo -e "Poison inject address should be greater than 0x1000."
	echo -e "Automatically setting address to 0x1000"
	ADDRESS=1000
fi

if [ -z $RUN_ON_QEMU ]; then
	log_normal "This test starts after applying 'noop' grouping policy."
	echo "$ cxl create-region -V -G noop"
	$CLI create-region -V -G noop
fi

# timestamp cmds
log_normal "[set-timestamp]"
echo "$ cxl set-timestamp mem0"
$CLI set-timestamp mem0

log_normal "[get-timestamp]"
echo "$ cxl get-timestamp mem0"
$CLI get-timestamp mem0

# poison cmds
# [Manual] Check poison list with cxl monitor
log_normal "[inject-poison]"
echo "$ cxl inject-poison mem0 -a 0x$ADDRESS"
$CLI inject-poison mem0 -a 0x$ADDRESS

log_normal "[clear-poison]"
echo "$ cxl clear-poison mem0 -a 0x$ADDRESS"
$CLI clear-poison mem0 -a 0x$ADDRESS

if [ -z $RUN_ON_QEMU ]; then
	# accessing poisoned address to generate event
	log_normal "[inject-poison]"
	echo "$ cxl inject-poison mem0 -a 0x$ADDRESS"
	$CLI inject-poison mem0 -a 0x$ADDRESS

	log_normal "Accessing poison injected address"
	$CLI destroy-region -V -w 1 cxl0
	$DAX_ACCESS_PGM 0x$ADDRESS
	$CLI create-region -V -G noop
fi

# event-record cmds
log_normal "[get-event-record]"
echo "$ cxl get-event-record mem0 -t 3"
$CLI get-event-record mem0 -t 3

log_normal "[clear-event-record]"
echo "$ cxl clear-event-record mem0 -t 3 -a"
$CLI clear-event-record mem0 -t 3 -a

if [ -z $RUN_ON_QEMU ]; then
	log_normal "[get-event-record]"
	echo "$ cxl get-event-record mem0 -t 3"
	$CLI get-event-record mem0 -t 3

	log_normal "[clear-poison]"
	echo "$ cxl clear-poison mem0 -a 0x$ADDRESS"
	$CLI clear-poison mem0 -a 0x$ADDRESS
fi

# identify cmd
log_normal "[identify]"
echo "$ cxl identify mem0"
$CLI identify mem0

if [ -z $RUN_ON_QEMU ]; then
	# health info and alerts cmds
	log_normal "[get-health-info]"
	echo "$ cxl get-health-info mem0"
	$CLI get-health-info mem0

	log_normal "[get-alert-config]"
	echo "$ cxl get-alert-config mem0"
	$CLI get-alert-config mem0

	# Shutdown State cmds
	log_normal "[set-shutdown-state]"
	echo "$ cxl set-shutdown-state mem0"
	$CLI set-shutdown-state mem0

	log_normal "[get-shutdown-state]"
	echo "$ cxl get-shutdown-state mem0"
	$CLI get-shutdown-state mem0

	log_normal "[set-shutdown-state]"
	echo "$ cxl set-shutdown-state mem0 --clean"
	$CLI set-shutdown-state mem0 --clean

	log_normal "[get-shutdown-state]"
	echo "$ cxl get-shutdown-state mem0"
	$CLI get-shutdown-state mem0
else
	# get firmware info
	log_normal "[get-firmware-info]"
	echo "$ cxl get-firmware-info mem0"
	$CLI get-firmware-info mem0
fi

# Scan Media and Sanitize cmds: test_cli_backgroud_cmd.sh

# SLD QoS Control cmds
log_normal "[get-sld-qos-control]"
echo "$ cxl get-sld-qos-control mem0"
$CLI get-sld-qos-control mem0

log_normal "[set-sld-qos-control]"
echo "$ cxl set-sld-qos-control mem0 -e -t -m 50 -s 75 -i 10"
$CLI set-sld-qos-control mem0 -e -t -m 50 -s 75 -i 10

log_normal "[get-sld-qos-status]"
echo "$ cxl get-sld-qos-status mem0"
$CLI get-sld-qos-status mem0

exit 0
