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

TEST="[group-dax dev0]"
log_normal "[group-dax dev0]"
$CLI group-dax --dev cxl0
check_result $?

TEST="[group-dax (all)]"
log_normal "[group-dax (all)]"
$CLI group-dax
check_result $?

TEST="[group-zone]"
log_normal "[group-zone]"
$CLI group-zone
check_result $?

TEST="[group-node]"
log_normal "[group-node]"
$CLI group-node
check_result $?

TEST="[group-noop]"
log_normal "[group-noop]"
$CLI group-noop
check_result $?

log_normal "[group-list --dev]"
$CLI group-list --dev

log_normal "[group-list --dev cxl0]"
$CLI group-list --dev cxl0

log_normal "[group-list --node]"
$CLI group-list --node

log_normal "[group-list --node 1]"
$CLI group-list --node 1

TEST="[group-remove --dev cxl0]"
log_normal "[group-remove --dev cxl0]"
$CLI group-remove --dev cxl0
check_result $?

TEST="[group-remove --node 1]"
log_normal "[group-remove --node 1]"
$CLI group-remove --node 1
check_result $?

TEST="[group-add --target_node 1 --dev cxl0]"
log_normal "[group-add --target_node 1 --dev cxl0]"
$CLI group-add --target_node 1 --dev cxl0
check_result $?

$CLI group-zone

echo
if [ $TEST_FAIL -eq 0 ]; then
    echo "PASS"
else
    echo "FAIL"
    exit 1
fi

exit 0

