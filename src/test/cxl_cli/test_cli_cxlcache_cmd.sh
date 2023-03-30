#!/bin/bash

### prerequisite:
### 1. CXL-CLI
### 2. CXLCache module
### 3. Root privilege

readonly BASEDIR=$(readlink -f $(dirname $0))/../../..
source "$BASEDIR/script/common.sh"

CLI=$BASEDIR/lib/cxl_cli/build/cxl/cxl
ENABLED=/sys/module/cxlcache/parameters/enabled
TC_FAIL=0

if [ ! -f "$CLI" ]; then
	log_error "CXL-CLI does not exist. Run 'build_lib.sh cxl_cli' in /path/to/SMDK/lib/"
	exit 2
fi

if [ ! -d "/sys/module/cxlcache/" ]; then
	log_error "CXLCache does not exist. Install CXLCache module (turn on CXLCache options when building SMDK kernel)."
	exit 2
fi

if [ `whoami` != 'root' ]; then
	log_error "This test requires root privileges"
	exit 2
fi

function print_cxlcache_status() {
	STATUS=`cat $ENABLED`
	printf "[CXLCache status] "
	if [ $STATUS == 'Y' ]; then
		printf "enabled\n"
	else
		printf "disabled\n"
	fi
}

function check_result() {
	if [ $1 -ne $2 ]; then
		TC_FAIL=1
		log_error "FAILED"
	else
		log_normal "PASSED"
	fi
}

function run_test() {
	# $1=test name, $2=test env
	if [ $2 == enable ]; then
		echo 1 > $ENABLED
	else
		echo 0 > $ENABLED
	fi
	print_cxlcache_status
	echo ""
	echo "[CXL-CLI] $1"
	$CLI $1
	RET=$?
}


INIT=`cat $ENABLED`

log_normal "[disable-cxlcache]"

run_test "disable-cxlcache" "enable"
echo ""
print_cxlcache_status
check_result $RET 0

log_normal "[enable-cxlcache]"

run_test "enable-cxlcache" "disable"
echo ""
print_cxlcache_status
check_result $RET 0


log_normal "[check-cxlcache]"

run_test "check-cxlcache" "disable"
check_result $RET 0

run_test "check-cxlcache" "enable"
check_result $RET 0


log_normal "[flush-cxlcache]"

run_test "flush-cxlcache" "enable"
check_result $RET 1

run_test "flush-cxlcache" "disable"
check_result $RET 0


if [ $INIT == 'Y' ]; then
	echo 1 > $ENABLED
else
	echo 0 > $ENABLED
fi


echo ""
if [ $TC_FAIL -eq 0 ]; then
	log_normal "TC PASS"
else
	log_error "TC FAIL"
	exit 1
fi

exit 0
