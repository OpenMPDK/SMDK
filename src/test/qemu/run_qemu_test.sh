#!/usr/bin/env bash
### prerequisite:
### 1. SMDK Kernel is running on QEMU environment.
### 2. $BASEDIR/lib/build_lib.sh cxl_cli

readonly BASEDIR=$(readlink -f $(dirname $0))/../../../
source "$BASEDIR/script/common.sh"

CXLCLI_DIR=$BASEDIR/lib/cxl_cli/
CXLCLI=$CXLCLI_DIR/build/cxl/cxl
DAXCTL=$CXLCLI_DIR/build/daxctl/daxctl

readonly TEST_SUCCESS=0
readonly TEST_FAILURE=1
readonly ENV_SET_FAIL=2

function check_privilege() {
	if [ $EUID -ne 0 ]; then
		log_error "This test requires root privileges."
		exit $ENV_SET_FAIL
	fi
}

function check_binaries() {
	if [ ! -f "$CXLCLI" ]; then
		log_error "cxl does not exist. Run 'build_lib.sh cxl_cli' in /path/to/SMDK/lib."
		exit $ENV_SET_FAIL
	fi

	if [ ! -f "$DAXCTL" ]; then
		log_error "daxctl doest not exist. Run 'build_lib.sh cxl_cli' in /path/to/SMDK/lib."
		exit $ENV_SET_FAIL
	fi
}

function check_region_created() {
	REGION_IOMEM=`sudo cat /proc/iomem | grep region0`
	if [ ! -z "$REGION_IOMEM" ]; then
		log_error "cxl-region is already created. Reconfigure-device to devdax and destory cxl-region."
		exit $ENV_SET_FAIL
	fi
}

REGION_SIZE=$((1*1024*1024*1024))
function test_cxl_create_region() {
	log_normal "Run testcase - cxl create-region"
	$CXLCLI create-region -d decoder0.0 -s $REGION_SIZE -t ram
	ret=$?
	if [ $ret -ne 0 ]; then
		log_error "cxl create-region failed."
		exit $TEST_FAILURE
	fi
	sleep 2
	cat /proc/buddyinfo
}

function test_offline() {
	log_normal "Run testcase - daxctl reconfigure-device --mode=devdax"
	$DAXCTL reconfigure-device --mode=devdax dax0.0 -f
	ret=$?
	if [ $ret -ne 0 ]; then
		log_error "daxctl reconfigure-device --mode=daxdav failed."
		exit $TEST_FAILURE
	fi
	cat /proc/buddyinfo
}

function test_online() {
	log_normal "Run testcase - daxctl reconfigure-device --mode=system-ram"
	$DAXCTL reconfigure-device --mode=system-ram dax0.0 -f
	ret=$?
	if [ $ret -ne 0 ]; then
		log_error "daxctl reconfigure-device --mode=system-ram failed."
		exit $TEST_FAILURE
	fi
	cat /proc/buddyinfo
}

check_privilege
check_binaries
check_region_created

test_cxl_create_region
test_offline
test_online
