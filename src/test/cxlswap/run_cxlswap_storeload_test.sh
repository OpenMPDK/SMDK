#!/bin/bash

# Written by seungjun.ha@samsung.com at 2022 November
# Purpose for test CXL Swap Load


# Enviroment for test - 1. CXL Swap / 2. Cgroup
# Cgroup need for limit the process memory max usage.
# Limit process memory using cgroup can occur swap earlier than using all system memory.
# If your system don't have both one, then test will not run.
# So, please prepare CXL Swap and Cgroup on your system before test.

# Because CXL Swap is System Module, Pass/Fail criteria are not rigidly set.
# But if there are failed or error when configuring the environment while test, check the output.

TEST_SUCCESS=0
TEST_FAILURE=1
ENV_SET_FAIL=2

readonly BASEDIR=$(readlink -f $(dirname $0))/../../../

if [ ! -e $BASEDIR/script/common.sh ]; then
	echo common.sh Not Exist.
	echo Please locate TC under /path/to/dmdk.cxlmalloc/src/test/cxlswap/
	exit $ENV_SET_FAIL
fi
source "$BASEDIR/script/common.sh"

#Binary Path
CXLSWAP_TEST=$BASEDIR/src/test/cxlswap/test_cxlswap
if [ ! -e $CXLSWAP_TEST ]; then
	echo Binary File Not Exist
	echo Please make before run
	exit $ENV_SET_FAIL
fi

function check_privilege() {
	if [ $EUID -ne 0 ]; then
		echo Please Run as Root if you want to modify status
		exit $ENV_SET_FAIL
	fi
}

CXLSWAPMODULE="/sys/module/cxlswap/parameters/enabled"
function check_cxlswap_exist() {
	if [ ! -e $CXLSWAPMODULE ]; then
		echo Cannot Modify CXL Swap Status. Module Not Exist
		exit $ENV_SET_FAIL
	fi
}

CXLSWAP_STATUS=$(cat ${CXLSWAPMODULE})
function modify_cxlswap_to_enabled() {
	if [ $CXLSWAP_STATUS = "N" ]; then
		echo 1 > $CXLSWAPMODULE
		if [ $? -ne 0 ]; then
			echo Cannot modify CXL Swap Status to Enabled
			exit $ENV_SET_FAIL
		fi
	fi
}

################################# Run Test #################################
# 1. store_load
# Description
# - The page is swap out by store and swap in by load repeatedly.
# - If you run TC with root privilege, you can check before & after stored pages in CXL Swap. (Cause it is system value, not totally but approximately correct)
# Pass/Fail
# - Pass : All data after swap in are same with before swap out.
# - Fail : Failure to meet the criteria for pass.
# - Error : mmap failure

echo Store Load Test Start
check_privilege
check_cxlswap_exist
modify_cxlswap_to_enabled

for ((i = 90; i > 50; i -= 20));
do
	for j in 512m 1g;
	do
		$CXLSWAP_TEST $j $i store_load
		RETURN=$?
		if [ $RETURN -eq $ENV_SET_FAIL ]; then
			echo Environment setting for Test Fail.
			exit $ENV_SET_FAIL
		elif [ $RETURN -eq $TEST_FAILURE ]; then
			echo CXL Swap Test Fail.
			exit $TEST_FAILURE
		fi
	done
done
echo Store Load Test Finish

exit $TEST_SUCCESS
############################################################################
