#!/bin/bash

readonly BASEDIR=$(readlink -f $(dirname $0))/../../..
source "$BASEDIR/script/common.sh"

CLI=$BASEDIR/lib/cxl_cli/build/cxl/cxl
TEST_FAIL=0

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
    if [ $RET -ne 0 ]; then
        TEST_FAIL=1
        echo "Test FAIL at $TEST"
    fi
    print_sysinfo
}

log_normal "[create-region -V -G node]"
$CLI create-region -V -G node
check_result $?

log_normal "[create-region -V -G noop]"
$CLI create-region -V -G noop
check_result $?

log_normal "[list -V --list_dev]"
$CLI list -V --list_dev

log_normal "[-V --list_dev cxl0]"
$CLI list -V --list_dev cxl0

log_normal "[list -V --list_node]"
$CLI list -V --list_node

log_normal "[list -V --list_node 1]"
$CLI list -V --list_node 1

log_normal "[destroy-region -V -w 1 cxl0]"
$CLI destroy-region -V -w 1 cxl0
check_result $?

log_normal "[destroy-region -V -N 1]"
$CLI destroy-region -V -N 1
check_result $?

log_normal "[create-region -V -N 1 -w 1 cxl0]"
$CLI create-region -V -N 1 -w 1 cxl0
check_result $?

$CLI create-region -V -G noop

echo
if [ $TEST_FAIL -eq 0 ]; then
    echo "PASS"
else
    echo "FAIL"
    exit 1
fi

exit 0

