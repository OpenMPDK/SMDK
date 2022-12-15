#!/bin/bash

readonly BASEDIR=$(readlink -f $(dirname $0))/../../..
source "$BASEDIR/script/common.sh"

CLI=$BASEDIR/lib/cxl_cli/build/cxl/cxl
TEST_TOTAL_COUNT=0
TEST_PASS_COUNT=0

if [ `whoami` != 'root' ]; then
	echo "This test requires root privileges"
	exit 2
fi

function print_sysinfo() {
	echo -e "\n\t/proc/buddyinfo \n\n"
	cat /proc/buddyinfo
	echo ""
	echo -e "\n\t/proc/iomem CXL related info\n\n"
	cat /proc/iomem | grep -e "hmem" -e "cxl" -e "Soft" -e "dax"
	echo ""
}

function check_result() {
	RET=$1
	TEST_TOTAL_COUNT=$(($TEST_TOTAL_COUNT + 1))
	if [ $RET -ne 0 ]; then
		TEST_PASS_COUNT=$(($TEST_PASS_COUNT + 1))
	else
		echo "Test Fail at Test #$TEST_TOTAL_COUNT!"
	fi
}


$CLI group-dax --inval
check_result $?
$CLI group-dax --dev
check_result $?
$CLI group-dax --dev cxl99
check_result $?
$CLI group-dax --dev mem0
check_result $?

$CLI group-list --inval
check_result $?
$CLI group-list --dev cxl99
check_result $?
$CLI group-list --dev mem0
check_result $?
$CLI group-list --node 99
check_result $?

$CLI group-add --inval
check_result $?
$CLI group-add --target_node 0
check_result $?
$CLI group-add --dev cxl0
check_result $?
$CLI group-add --target_node 99 --dev cxl0
check_result $?
$CLI group-add --target_node 0 --dev cxl99
check_result $?

$CLI group-remove --inval
check_result $?
$CLI group-remove --dev
check_result $?
$CLI group-remove --dev cxl99
check_result $?
$CLI group-remove --dev mem0
check_result $?
$CLI group-remove --node
check_result $?
$CLI group-remove --node 99
check_result $?

echo "Test Total: $TEST_TOTAL_COUNT, Test Pass: $TEST_PASS_COUNT"

echo
if [ $TEST_TOTAL_COUNT == $TEST_PASS_COUNT ]; then
	echo "PASS"
else
	echo "FAIL"
	exit 1
fi

exit 0

