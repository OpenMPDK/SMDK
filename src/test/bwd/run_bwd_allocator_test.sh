#!/usr/bin/env bash
###################################################################
# prerequisite : SMDK Kernel is running
###################################################################

readonly BASEDIR=$(readlink -f $(dirname $0))/../../../
source "$BASEDIR/script/common.sh"

readonly TEST_SUCCESS=0
readonly TEST_FAILURE=1
readonly ENV_SET_FAIL=2

readonly SCRIPT_PATH=$(readlink -f $(dirname $0))/

readonly SMDKLIB_PATH=$BASEDIR/lib/smdk_allocator/lib/libcxlmalloc.so
readonly node_count=$(ls -al /sys/devices/system/node/node*/cpulist | wc -l)

STRING_FEATURE_IS_DISABLED="use_adaptive_interleaving is disabled"
STRING_NORMAL_ZONE_SIZE_UNLIMITED="normal_zone_size = unlimited"
STRING_EXMEM_ZONE_SIZE_UNLIMITED="exmem_zone_size = unlimited"
STRING_MAXMEMORY_POLICY_IGNORED="maxmemory_policy = ignored"
STRING_PRIORITY_NORMAL="prio = [normal->exmem]"
STRING_MAXMEMORY_POLICY_INTERLEAVE="maxmemory_policy = interleave"
STRING_BW_SATURATION_POLICY="adaptive_interleaving_policy = bw_saturation"

function check_bwd_running() {
	bwd_run_count=$(ps uafx | grep -x "\<bwd\>" | grep -v grep | wc -l)
	if [ $bwd_run_count -ne 0 ]; then
		echo "Stop bwd daemon and try again"
		exit $ENV_SET_FAIL
	fi
}

function check_binaries() {
	if [ ! -f "$SMDKLIB_PATH" ]; then
		echo "libcxlmalloc.so does not exist. Run 'build_lib.sh smdkmalloc' in /path/to/SMDK/lib"
		exit $ENV_SET_FAIL
	fi
}

function prepare_test() {
	check_binaries
	check_bwd_running
}

function bwd_mock_start() {
	sudo mkdir /run/bwd/

	while IFS='/' read -r _ _ _ _ _ node _; do
		nids+=("${node//node/}")
	done < <(find /sys/devices/system/node -name cpulist)

	for number in "${nids[@]}"; do
		echo -n false | sudo tee /run/bwd/node$number > /dev/null
	done	
}

function bwd_mock_end() {
	sudo rm -rf /run/bwd/

}

TEST_FAIL=0
TEST_PASS=0

testcase=( t1 t2 t3 t4 t5 t6 t7 )

function t1() {
	# Without /run/bwd, adaptive interleaving should be disabled
	CXLMALLOC_CONF=use_exmem:true,use_adaptive_interleaving:true
	OUTPUT_FILE=t1.log
	run_testcase $CXLMALLOC_CONF $OUTPUT_FILE
	check_result "$STRING_FEATURE_IS_DISABLED" $OUTPUT_FILE
	rm -rf $OUTPUT_FILE
}

function t2() {
	# If adaptive interleaving is enabled, normal_zone_size should be ignored
	bwd_mock_start
	CXLMALLOC_CONF=use_exmem:true,use_adaptive_interleaving:true,normal_zone_size:1G
	OUTPUT_FILE=t2.log
	run_testcase $CXLMALLOC_CONF $OUTPUT_FILE
	check_result "$STRING_NORMAL_ZONE_SIZE_UNLIMITED" $OUTPUT_FILE
	rm -rf $OUTPUT_FILE
	bwd_mock_end
}

function t3() {
	# If adaptive interleaving is enabled, exmem_zone_size should be ignored
	bwd_mock_start
	CXLMALLOC_CONF=use_exmem:true,use_adaptive_interleaving:true,exmem_zone_size:1G
	OUTPUT_FILE=t3.log
	run_testcase $CXLMALLOC_CONF $OUTPUT_FILE
	check_result "$STRING_EXMEM_ZONE_SIZE_UNLIMITED" $OUTPUT_FILE
	rm -rf $OUTPUT_FILE
	bwd_mock_end
}

function t4() {
	# If adaptive interleaving is enabled, maxmemory_policy should be ignored
	bwd_mock_start
	CXLMALLOC_CONF=use_exmem:true,use_adaptive_interleaving:true,maxmemory_policy:interleave
	OUTPUT_FILE=t4.log
	run_testcase $CXLMALLOC_CONF $OUTPUT_FILE
	check_result "$STRING_MAXMEMORY_POLICY_IGNORED" $OUTPUT_FILE
	rm -rf $OUTPUT_FILE
	bwd_mock_end
}

function t5() {
	# If adaptive interleaving is enabled, priority should be normal->exmem
	bwd_mock_start
	CXLMALLOC_CONF=use_exmem:true,use_adaptive_interleaving:true,priority:exmem
	OUTPUT_FILE=t5.log
	run_testcase $CXLMALLOC_CONF $OUTPUT_FILE
	check_result "$STRING_PRIORITY_NORMAL" $OUTPUT_FILE
	rm -rf $OUTPUT_FILE
	bwd_mock_end
}

function t6() {
	# If adaptive interleaving is enabled, maxmemory_policy should be ignored
	bwd_mock_start
	CXLMALLOC_CONF=use_exmem:true,use_adaptive_interleaving:false,maxmemory_policy:interleave
	OUTPUT_FILE=t6.log
	run_testcase $CXLMALLOC_CONF $OUTPUT_FILE
	check_result "$STRING_MAXMEMORY_POLICY_INTERLEAVE" $OUTPUT_FILE
	rm -rf $OUTPUT_FILE
	bwd_mock_end
}

function t7() {
	# If adaptive interleaving is enabled and adaptive_interleaving_policy is wrong, bw_saruation should be used
	bwd_mock_start
	CXLMALLOC_CONF=use_exmem:true,use_adaptive_interleaving:true,adaptive_interleaving_policy:invalid_policy
	OUTPUT_FILE=t7.log
	run_testcase $CXLMALLOC_CONF $OUTPUT_FILE
	check_result "$STRING_BW_SATURATION_POLICY" $OUTPUT_FILE
	rm -rf $OUTPUT_FILE
	bwd_mock_end
}

function run_testcase() {
    CXLMALLOC_CONF=$1
    export CXLMALLOC_CONF
    LD_PRELOAD=$SMDKLIB_PATH CXLMALLOC_CONF=$CXLMALLOC_CONF ls 2> "$2" 1> /dev/null
}

function run_test() {
	for i in "${testcase[@]}"; do
		log_normal "run testcase - $i"
		$i
	done
}

function check_result() {
	if grep -Fq "$1" "$2"; then
		echo "PASS"
		((TEST_PASS+=1));
	else
		((TEST_FAIL+=1));
	fi
}

prepare_test
run_test

printf "\n\n"
printf "Total ${#testcase[@]} TCs executed: "
printf " %-2s %-6s," "${TEST_PASS}" ${green}"PASSED"${rst}
printf " %-2s %-6s\n" "${TEST_FAIL}" ${red}"FAILED"${rst}

if [ $TEST_FAIL -ne 0 ]; then
	exit $TEST_FAILURE
else
	exit $TEST_SUCCESS
fi
