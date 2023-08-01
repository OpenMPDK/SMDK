#!/usr/bin/env bash
### prerequisite:
### 1. SMDK Kernel is running

### SCRIPT_PATH : directory where this test script exist
readonly SCRIPT_PATH=$(readlink -f $(dirname $0))
source "$SCRIPT_PATH/bwd_common.sh"

echo $SCRIPT_PATH

TEST_RESULT=$TEST_SUCCESS

APPNAME=bwd

function check_privilege() {
    if [ $EUID -ne 0 ]; then
        log_error "This test requires root privileges"
        exit $ENV_SET_FAIL
    fi
}

function check_binaries() {
    if [ ! -f "$BWD_DD" ]; then
        log_error "$APPNAME.ko does not exist. Run 'build_lib.sh bwd' in /path/to/SMDK/lib/"
        exit $ENV_SET_FAIL
    fi
}

function check_precondition() {
    MODULE_INSTALLED=$(lsmod | grep $APPNAME)
    ret=$?
    if [ $ret -eq 0 ]; then
        log_error "$APPNAME.ko is already installed. Clean up before testing."
        exit $ENV_SET_FAIL
    fi
}

function prepare_test() {
    check_privilege
    check_binaries
    check_precondition
}

function run_test() {
    insmod $BWD_DD
    ret=$?
    if [ $ret -ne 0 ]; then
        log_error "insmod $APPNAME.ko failed."
        echo "FAIL"
        exit $ENV_SET_FAIL
    fi

    if [ ! -e "$BWD_DEVPATH" ]; then
        log_error "$BWD_DEVPATH does not exist."
        TEST_RESULT=$TEST_FAILURE
        return
    fi

    TEST_RESULT=$TEST_SUCCESS
}

function finalize_test() {
    rmmod $APPNAME
    ret=$?
    if [ $ret -ne 0 ]; then
        log_error" rmmod $APPNAME failed."
    fi
}

function check_result() {
    if [ "$TEST_RESULT" = "$TEST_FAILURE" ]; then
        echo "FAIL"
        exit $TEST_FAILURE
    else
        echo "PASS"
        exit $TEST_SUCCESS
    fi
}

prepare_test
run_test
finalize_test
check_result
