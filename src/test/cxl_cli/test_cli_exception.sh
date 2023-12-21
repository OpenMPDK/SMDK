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


$CLI list -V --list_dev --inval
check_result $?
$CLI list -V --list_dev cxl99
check_result $?
$CLI list -V --list_dev mem0
check_result $?
$CLI list -V --list_node 99
check_result $?

$CLI create-region -V -G inval
check_result $?
$CLI create-region -V -N cxl0
check_result $?
$CLI create-region -V -N 2 -w 2 cxl0
check_result $?
$CLI create-region -V -N 99 -w 1 cxl0
check_result $?
$CLI create-region -V -N 2 -w 1 cxl99
check_result $?

$CLI destroy-region -V --inval
check_result $?
$CLI destroy-region -V -w 2 cxl0
check_result $?
$CLI destroy-region -V -w 1 cxl99
check_result $?
$CLI destroy-region -V -w 1 mem0
check_result $?
$CLI destroy-region -V -N
check_result $?
$CLI destroy-region -V -N 99
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

