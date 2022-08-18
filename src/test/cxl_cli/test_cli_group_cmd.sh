#!/bin/bash

readonly BASEDIR=$(readlink -f $(dirname $0))/../../..
source "$BASEDIR/script/common.sh"

CLI=$BASEDIR/lib/cxl_cli/build/cxl/cxl

if [ `whoami` != 'root' ]; then
	echo "This test requires root privileges"
	exit
fi

function print_sysinfo() {
	echo -e "\n\t/proc/buddyinfo \n\n"
	cat /proc/buddyinfo
	echo ""
	echo -e "\n\t/proc/iomem CXL related info\n\n"
	cat /proc/iomem | grep -e "hmem" -e "cxl" -e "Soft" -e "dax"
	echo ""
}

log_normal "[group-dax dev0]"
$CLI group-dax --dev cxl0
print_sysinfo

log_normal "[group-dax (all)]"
$CLI group-dax
print_sysinfo

log_normal "[group-zone]"
$CLI group-zone
print_sysinfo

log_normal "[group-node]"
$CLI group-node
print_sysinfo

log_normal "[group-noop]"
$CLI group-noop
print_sysinfo

log_normal "[group-list --dev]"
$CLI group-list --dev

log_normal "[group-list --dev cxl0]"
$CLI group-list --dev cxl0

log_normal "[group-list --node]"
$CLI group-list --node

log_normal "[group-list --node 1]"
$CLI group-list --node 1

log_normal "[group-remove --dev cxl0]"
$CLI group-remove --dev cxl0
print_sysinfo

log_normal "[group-remove --node 1]"
$CLI group-remove --node 1
print_sysinfo

log_normal "[group-add --target_node 1 --dev cxl0]"
$CLI group-add --target_node 1 --dev cxl0
print_sysinfo
