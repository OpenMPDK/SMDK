#!/usr/bin/env bash
### prerequisite:
### 1. SMDK Kernel is running

### SCRIPT_PATH : directory where this test script exist
readonly SCRIPT_PATH=$(readlink -f $(dirname $0))
source "$SCRIPT_PATH/bwd_common.sh"

TEST_RESULT=$TEST_SUCCESS

APPNAME=bwd
UPROF=$BWDDIR/AMDuProf_Linux_x64_4.0.341/bin/AMDuProfPcm
PCM=$BWDDIR/pcm/build/bin/pcm-memory

CPU_VENDOR=`cat /proc/cpuinfo | grep vendor_id | uniq | awk '{print $3}'`

function check_privilege() {
	if [ $EUID -ne 0 ]; then
		log_error "This test requires root privileges."
		exit $ENV_SET_FAIL
	fi
}

function check_binary() {
	if [ "$CPU_VENDOR" = "AuthenticAMD" ]; then
		if [ ! -f $UPROF ]; then
			log_error "AMDuProfPCM binary does not exist. Run 'build_lib.sh bwd' in /path/to/SMDK/lib/."
			exit $ENV_SET_FAIL
		elif [ ! -f $PCM ]; then
			log_error "Intel PCM binary does not exist. Run 'build_lib.sh bwd' in /path/to/SMDK/lib/."
			exit $ENV_SET_FAIL
		fi
	fi
}


function prepare_test() {
	check_privilege
	check_binary
}

function run_test() {
	if [ "$CPU_VENDOR" = "AuthenticAMD" ]; then
		$UPROF -a -m memory -d -1 &
		UPROFPID=$!
		if [ $UPROFPID -ne `ps -ef | grep "$UPROFPID" | head -n 1 | awk '{print $2}'` ]; then
			log_error "AMDuProfPCM does not operate."
			TEST_RESULT=$TEST_FAILURE
			kill $UPROFPID
            return
		fi
		kill $UPROFPID
	elif [ "$CPU_VENDOR" = "GenuineIntel" ]; then
		$PCM -u &
		PCMPID=$!
		if [ $PCMPID -ne `ps -ef | grep "$PCMPID" | head -n 1 | awk '{print $2}'` ]; then
			log_error "Intel PCM does not operate."
			TEST_RESULT=$TEST_FAILURE
			kill $PCMPID
            return
		fi
		kill $PCMPID
	fi

	TEST_RESULT=$TEST_SUCCESS
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
check_result
