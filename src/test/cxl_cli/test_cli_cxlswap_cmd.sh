#!/bin/bash

### prerequisite:
### 1. CXL-CLI
### 2. CXLSwap module
### 3. Root privilege

readonly BASEDIR=$(readlink -f $(dirname $0))/../../..
source "$BASEDIR/script/common.sh"

CLI=$BASEDIR/lib/cxl_cli/build/cxl/cxl
ENABLED=/sys/module/cxlswap/parameters/enabled
TC_FAIL=0

if [ ! -f "$CLI" ]; then
	log_error "CXL-CLI does not exist. Run 'build_lib.sh cxl_cli' in /path/to/SMDK/lib/"
	exit 2
fi

if [ ! -d "/sys/module/cxlswap/" ]; then
	log_error "CXLSwap does not exist. Install CXLSwap module (turn on CXLSwap options when building SMDK kernel)."
	exit 2
fi

if [ `whoami` != 'root' ]; then
	log_error "This test requires root privileges"
	exit 2
fi

function print_cxlswap_status() {
	STATUS=`cat $ENABLED`
	printf "[CXLSwap status] "
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
	print_cxlswap_status
	echo ""
	echo "[CXL-CLI] $1"
	$CLI $1
	RET=$?
}


INIT=`cat $ENABLED`

log_normal "[disable-cxlswap]"

run_test "disable-cxlswap" "enable"
echo ""
print_cxlswap_status
check_result $RET 0

log_normal "[enable-cxlswap]"

run_test "enable-cxlswap" "disable"
echo ""
print_cxlswap_status
check_result $RET 0


log_normal "[check-cxlswap]"

run_test "check-cxlswap" "disable"
check_result $RET 0

run_test "check-cxlswap" "enable"
check_result $RET 0


log_normal "[flush-cxlswap]"

run_test "flush-cxlswap" "enable"
check_result $RET 1

run_test "flush-cxlswap" "disable"
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

