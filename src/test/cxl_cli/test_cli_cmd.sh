#!/bin/bash

readonly BASEDIR=$(readlink -f $(dirname $0))/../../..
source "$BASEDIR/script/common.sh"

CLI=$BASEDIR/lib/cxl_cli/build/cxl/cxl
ADDRESS=$1
DAX_ACCESS_PGM=$BASEDIR/src/test/cxl_cli/devmem2

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

log_normal "This test starts after applying 'Zone' grouping policy."
log_normal "[group-zone]"
echo "$ cxl group-zone"
$CLI group-zone

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

# accessing poisoned address to generate event
log_normal "[inject-poison]"
echo "$ cxl inject-poison mem0 -a 0x$ADDRESS"
$CLI inject-poison mem0 -a 0x$ADDRESS

log_normal "Accessing poison injected address"
$CLI group-dax
$DAX_ACCESS_PGM 0x$ADDRESS
$CLI group-zone

# event-record cmds
log_normal "[get-event-record]"
echo "$ cxl get-event-record mem0 -t 3"
$CLI get-event-record mem0 -t 3

log_normal "[clear-event-record]"
echo "$ cxl clear-event-record mem0 -t 3 -a"
$CLI clear-event-record mem0 -t 3 -a

log_normal "[get-event-record]"
echo "$ cxl get-event-record mem0 -t 3"
$CLI get-event-record mem0 -t 3

log_normal "[clear-poison]"
echo "$ cxl clear-poison mem0 -a 0x$ADDRESS"
$CLI clear-poison mem0 -a 0x$ADDRESS

# identify cmd
log_normal "[identify]"
echo "$ cxl identify mem0"
$CLI identify mem0

# health info and alerts cmds
log_normal "[get-health-info]"
echo "$ cxl get-health-info mem0"
$CLI get-health-info mem0

log_normal "[get-alert-config]"
echo "$ cxl get-alert-config mem0"
$CLI get-alert-config mem0

log_normal "[set-alert-config]"
echo "$ cxl set-alert-config mem0 -e life_used -a enable -t 50"
$CLI set-alert-config mem0 -e life_used -a enable -t 50

echo "$ cxl set-alert-config mem0 -e over_temperature -a enable -t 60"
$CLI set-alert-config mem0 -e over_temperature -a enable -t 60

echo "$ cxl set-alert-config mem0 -e under_temperature -a disable"
$CLI set-alert-config mem0 -e under_temperature -a disable

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

exit 0
