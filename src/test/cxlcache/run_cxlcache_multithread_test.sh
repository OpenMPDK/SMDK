#!/bin/bash

readonly TCDIR=$(readlink -f $(dirname $0))
source "$TCDIR/cxlcache_common.sh"

### prerequisite:
### 1. SMDK Kernel is running
### 2. CXL Cache must be enabled
### 3. Please run with root privilege.

################################# Run Test #################################
# 3. multi_thread test
# Description
# - Create test files as many as the number of test threads and put the data at CXL Cache.
# - Create child threads and wait for all threads to finish.
# - Each child thread gets own file from CXL Cache and put in CXL Cache again after modifying the data.
# - Check that the data getting from CXL Cache is same with modified contents.
# Pass/Fail
# - Pass : All data after page cache drop are same with before page cache drop.
# - Fail : Failure to meet the criteria for pass.
# - Error : mmap, file operations, cxlcache config failure.

echo Multi Thread Test Start
check_privilege
check_movable_exist
check_cxlcache_exist
check_dir_fs_type
modify_cxlcache_to_enabled

if [ -z $RUN_ON_QEMU ]; then
	SIZE=("128m" "256m")
	NUM_DIR=10
else
	SIZE=("16m" "32m")
	NUM_DIR=3
fi

for i in "${SIZE[@]}";
do
	$CXLCACHE_TEST $i multi_thread $TEST_DIR $NUM_DIR
	RETURN=$?
	if [ $RETURN -eq $ENV_SET_FAIL ]; then
		echo Environment setting for Test Fail.
		exit $ENV_SET_FAIL
	elif [ $RETURN -eq $TEST_FAILURE ]; then
		echo CXL Cache Test Fail.
		exit $TEST_FAILURE
	fi
done
echo Multi Thread Test Succ.

exit $TEST_SUCCESS
############################################################################
