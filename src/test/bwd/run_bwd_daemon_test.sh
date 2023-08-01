#!/usr/bin/env bash
### prerequisite:
### 1. SMDK Kernel is running

### SCRIPT_PATH : directory where this test script exist
readonly SCRIPT_PATH=$(readlink -f $(dirname $0))
source "$SCRIPT_PATH/bwd_common.sh"

TEST_RESULT=$TEST_SUCCESS

APPNAME=bwd

function check_privilege() {
	if [ $EUID -ne 0 ]; then
		log_error "This test requires root privileges."
		exit $ENV_SET_FAIL
	fi
}

function check_binaries() {
	if [ ! -f "$BWD_DD" ]; then
		log_error "$APPNAME.ko does not exist. Run 'build_lib.sh bwd' in /path/to/SMDK/lib/."
		exit $ENV_SET_FAIL
	fi

	if [ ! -f "$BWD" ]; then
		log_error "$APPNAME binary does not exist. Run 'build_lib.sh bwd' in /path/to/SMDK/lib/."
		exit $ENV_SET_FAIL
	fi

	if [ ! -f "$BWD_CONFPATH" ]; then
		log_error "$APPNAME configuration does not exist. Check /path/to/SMDK/lib/bwd."
		exit $ENV_SET_FAIL
	fi
}

function check_precondition() {
	if [ ! -z `lsmod | grep $APPNAME | awk '{print $1}'` ]; then
		rmmod $BWD_DD
		ret=$?
		if [ $ret -ne 0 ]; then
			log_error "rmmod $APPNAME.ko failed."
			exit $ENV_SET_FAIL
		fi
	fi

	cd $BWDDIR
}

function prepare_test() {
	check_privilege
	check_binaries
	check_precondition
}

function run_test() {
	log_normal "Configurations for $APPNAME:"
	cat $BWD_CONFPATH | grep MLC
	cat $BWD_CONFPATH | grep AMD_UPROFPCM

	log_normal "Run testcase - run $APPNAME without $APPNAME.ko"
	$BWD -c $BWD_CONFPATH >& /dev/null
	ret=$?
	if [ $ret -eq 0 ]; then
		log_error "$APPNAME doesn't return error without $APPNAME driver insmod."
		TEST_RESULT=$TEST_FAILURE
		return
	fi

	echo PASS

	log_normal "Run testcase - $APPNAME should generate /run/bwd/nodeX"
	insmod $BWD_DD
	ret=$?
	if [ $ret -ne 0 ]; then
		log_error "insmod $APPNAME.ko failed"
		TEST_RESULT=$TEST_FAILURE
		return
	fi

	$BWD -c $BWD_CONFPATH >& /dev/null &
	BWD_PID=$!
	sleep 3

	NODE=`ls -al $BWDMAP | grep node | head -n 1 | awk '{print $9}'`
	if [ ! -e $BWDMAP/$NODE ]; then
		log_error "$APPNAME doesn't generate /run/bwd/nodeX."
		TEST_RESULT=$TEST_FAILURE
		return
	fi

	echo PASS

	log_normal "Run testcase - check SIGINT termination"
	kill -INT $BWD_PID

	if [ ! -e $BWDMAP ]; then
		log_error "$APPNAME doesn't remove /run/bwd/nodeX when exit successfully."
		TEST_RESULT=$TEST_FAILURE
		return
	fi

	echo PASS

	log_normal "Run testcase - $APPNAME should run well even if /dev/bwd is removed."
	$BWD -c $BWD_CONFPATH >& /dev/null &
	BWD_PID=$!
	sleep 3

	# Remove /dev/bwd
	rmmod $BWD_DD
	ret=$?
	if [ $ret -ne 0 ]; then
		log_error "rmmod $APPNAME.ko failed."
		TEST_RESULT=$TEST_FAILURE
		return
	fi

	# Test bwd
	kill -USR1 $BWD_PID
	sleep 3

	# Clean-up
	kill -INT $BWD_PID
	insmod $BWD_DD
	ret=$?
	if [ $ret -ne 0 ]; then
		log_error "insmod $APPNAME.ko failed"
		TEST_RESULT=$TEST_FAILURE
		return
	fi

	echo PASS

	log_normal "Run testcase - $APPNAME should check /run/bwd/nodeX existance."
	$BWD -c $BWD_CONFPATH >& /dev/null &
	BWD_PID=$!
	sleep 3
	BWD_CID=`pgrep -P $BWD_PID`

	kill -9 $BWD_PID
	for CID in $BWD_CID; do
		kill -INT $CID
	done

	BEF=`stat -c %Y $BWDMAP/$NODE`
	$BWD -c $BWD_CONFPATH >& /dev/null &
	BWD_PID=$!
	sleep 15
	AFT=`stat -c %Y $BWDMAP/$NODE`
	if [ $BEF -ge $AFT ]; then
		log_error "$APPNAME doesn't remove remaining nodeX when restart."
		TEST_RESULT=$TEST_FAILURE
		return
	fi

	kill -INT $BWD_PID

	echo PASS

	TEST_RESULT=$TEST_SUCCESS
}

function finalize_test() {
	if [ ! -z `lsmod | grep $APPNAME | awk '{print $1}'` ]; then
		rmmod $BWD_DD
		ret=$?
		if [ $ret -ne 0 ]; then
			log_error "rmmod $APPNAME.ko failed."
			exit $ENV_SET_FAIL
		fi
	fi
	cd -
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
