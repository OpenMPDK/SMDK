#!/usr/bin/env bash
### prerequisite:
### 1. SMDK Kernel is running

### SCRIPT_PATH : directory where this test script exist
readonly SCRIPT_PATH=$(readlink -f $(dirname $0))
source "$SCRIPT_PATH/tierd_common.sh"

TEST_RESULT=$TEST_SUCCESS

APPNAME=tierd

function check_privilege() {
	if [ $EUID -ne 0 ]; then
		log_error "This test requires root privileges."
		exit $ENV_SET_FAIL
	fi
}

function check_binaries() {
	if [ ! -f "$TIERD_DD" ]; then
		log_error "kmem.ko does not exist. Run 'build_lib.sh kernel' in /path/to/SMDK/lib/."
		exit $ENV_SET_FAIL
	fi

	if [ ! -f "$TIERD" ]; then
		log_error "$APPNAME binary does not exist. Run 'build_lib.sh tierd' in /path/to/SMDK/lib/."
		exit $ENV_SET_FAIL
	fi

	if [ ! -f "$TIERD_CONFPATH" ]; then
		log_error "$APPNAME configuration does not exist. Check /path/to/SMDK/lib/tierd."
		exit $ENV_SET_FAIL
	fi

	if [ ! -f "$DAXCTL" ]; then
		log_error "daxctl does not exist. Run 'build_lib.sh cxl_cli' in /path/to/SMDK/lib/."
		exit $ENV_SET_FAIL
	fi
}

function check_precondition() {
	if [ -n "`lsmod | awk '{print $1}' | grep kmem`" ]; then
		if [ ! -d "/sys/kernel/cxl/" ]; then
			log_error "kmem-ext module should be installed."
			exit $ENV_SET_FAIL
		fi

        arr=(`ls /dev/dax*`)
        for daxdev in ${arr[@]}; do
            $DAXCTL reconfigure-device --mode=devdax -f $daxdev
            if [ $? -ne 0 ]; then
                log_error "cxl group-dax failed."
                exit $ENV_SET_FAIL
            fi
        done

		rmmod $TIERD_DD
		ret=$?
		if [ $ret -ne 0 ]; then
			log_error "rmmod kmem.ko failed."
			exit $ENV_SET_FAIL
		fi
	fi

	cd $TIERDDIR
}

function prepare_test() {
	check_privilege
	check_binaries
	check_precondition
}

function run_test() {
	log_normal "Configurations for $APPNAME:"
	cat $TIERD_CONFPATH | grep MLC
	cat $TIERD_CONFPATH | grep AMD_UPROFPCM

	log_normal "Run testcase - run $APPNAME without kmem.ko"
	$TIERD -c $TIERD_CONFPATH >& /dev/null
	ret=$?
	if [ $ret -eq 0 ]; then
		log_error "$APPNAME doesn't return error without kmem driver insmod."
		TEST_RESULT=$TEST_FAILURE
		return
	fi

	echo PASS

	log_normal "Run testcase - $APPNAME should generate /run/tierd/nodeX"
	insmod $TIERD_DD
	ret=$?
	if [ $ret -ne 0 ]; then
		log_error "insmod kmem.ko failed"
		TEST_RESULT=$TEST_FAILURE
		return
	fi

	$TIERD -c $TIERD_CONFPATH >& /dev/null &
	TIERD_PID=$!
	sleep 3

	NODE=`ls -al $TIERDMAP | grep node | head -n 1 | awk '{print $9}'`
	if [ ! -e $TIERDMAP/$NODE ]; then
		log_error "$APPNAME doesn't generate /run/tierd/nodeX."
		TEST_RESULT=$TEST_FAILURE
		return
	fi

	echo PASS

	log_normal "Run testcase - check SIGINT termination"
	kill -INT $TIERD_PID

	echo PASS

	log_normal "Run testcase - $APPNAME should run well even if /dev/tierd is removed."
	$TIERD -c $TIERD_CONFPATH >& /dev/null &
	TIERD_PID=$!
	sleep 3

	# Remove /dev/tierd
	rmmod $TIERD_DD
	ret=$?
	if [ $ret -ne 0 ]; then
		log_error "rmmod kmem.ko failed."
		TEST_RESULT=$TEST_FAILURE
		return
	fi

	# Test tierd
	kill -USR1 $TIERD_PID
	sleep 3

	# Clean-up
	kill -INT $TIERD_PID
	insmod $TIERD_DD
	ret=$?
	if [ $ret -ne 0 ]; then
		log_error "insmod kmem.ko failed"
		TEST_RESULT=$TEST_FAILURE
		return
	fi

	echo PASS

	log_normal "Run testcase - $APPNAME should check /run/tierd/nodeX existance."
    sleep 2
	$TIERD -c $TIERD_CONFPATH >& /dev/null &
	TIERD_PID=$!
	sleep 2
	BEF=`stat -c %Y $TIERDMAP/$NODE/idle_state_bandwidth`
	if [ $? -ne 0 ]; then
		log_error "BWMAP doesn't be created."
		TEST_RESULT=$TEST_FAILURE
		return
	fi

	TIERD_CID=`pgrep -P $TIERD_PID`
	kill -9 $TIERD_PID
	for CID in $TIERD_CID; do
		kill -INT $CID
	done

	sleep 5
	$TIERD -c $TIERD_CONFPATH >& /dev/null &
	TIERD_PID=$!
	sleep 2
	AFT=`stat -c %Y $TIERDMAP/$NODE/idle_state_bandwidth`
	if [ $? -ne 0 ]; then
		log_error "BWMAP doesn't be recreated."
		TEST_RESULT=$TEST_FAILURE
		return
	fi

	kill -INT $TIERD_PID

	echo PASS

	TEST_RESULT=$TEST_SUCCESS
}

function finalize_test() {
	if [ -n "`lsmod | awk '{print $1}' | grep kmem`" ]; then
		rmmod $TIERD_DD
		ret=$?
		if [ $ret -ne 0 ]; then
			log_error "rmmod kmem.ko failed."
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
