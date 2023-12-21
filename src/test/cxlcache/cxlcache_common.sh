#!/bin/bash

readonly BASEDIR=$(readlink -f $(dirname $0))/../../../
source "$BASEDIR/script/common.sh"

readonly TEST_SUCCESS=0
readonly TEST_FAILURE=1
readonly ENV_SET_FAIL=2

### BINARY PATH
CXLCACHE_TEST=$BASEDIR/src/test/cxlcache/test_cxlcache
if [ ! -e $CXLCACHE_TEST ]; then
	echo Binary File Not Exist
	echo Please make before run
	exit $ENV_SET_FAIL
fi

TEST_DIR=$BASEDIR/src/test/cxlcache

function check_privilege() {
	if [ $EUID -ne 0 ]; then
		echo Please Run as Root if you want to modify status
		exit $ENV_SET_FAIL
	fi
}

CXLCACHEMODULE="/sys/module/cxlcache/parameters/enabled"
function check_cxlcache_exist() {
	if [ ! -e $CXLCACHEMODULE ]; then
		echo Cannot Modify CXL Cache Status. Module Not Exist
		exit $ENV_SET_FAIL
	fi
}

function modify_cxlcache_to_enabled() {
	CXLCACHE_STATUS=$(cat ${CXLCACHEMODULE})
	if [ $CXLCACHE_STATUS = "N" ]; then
		echo 1 > $CXLCACHEMODULE
		if [ $? -ne 0 ]; then
			echo Cannot modify CXL Cache Status to Enabled
			exit $ENV_SET_FAIL
		fi
	fi
}

function modify_cxlcache_to_disabled() {
	CXLCACHE_STATUS=$(cat ${CXLCACHEMODULE})
	if [ $CXLCACHE_STATUS = "Y" ]; then
		echo 0 > $CXLCACHEMODULE
		if [ $? -ne 0 ]; then
			echo Cannot modify CXL Cache Status to Disabled
			exit $ENV_SET_FAIL
		fi
	fi
}

function check_movable_exist() {
	ZONE_MOVABLE=$(cat /proc/buddyinfo | grep -i movable)
	ret=$?
	if [ $ret -ne 0 ]; then
		log_error "This test requires Movable zone."
		exit $ENV_SET_FAIL
	fi
}

function check_dir_fs_type() {
	FS_TYPE=`df -T $TEST_DIR | awk '{print $2}' | tail -n 1`
	if [ $FS_TYPE != "ext4" ]; then
		log_error "Test directory is not EXT4. CXL Cache only supports EXT4."
		exit $ENV_SET_FAIL
	fi
}
