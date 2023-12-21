#!/bin/bash

readonly TCDIR=$(readlink -f $(dirname $0))
source "$TCDIR/cxlcache_common.sh"

### prerequisite:
### 1. SMDK Kernel is running
### 2. CXL Cache must be enabled
### 3. Please run with root privilege.

################################# Run Test #################################
# 4. multi_process test
# Description
# - Fork child process and wait for child process to finish.
# - Child process puts the test file to CXL Cache and re-read from CXL Cache.
# - Child process modifies the data and exits after putting the data to CXL Cache.
# - Parent checks the data getting from CXL Cache matches the value modified by child.
# Pass/Fail
# - Pass : All data after page cache drop are same with before page cache drop.
# - Fail : Failure to meet the criteria for pass.
# - Error : mmap, file operations, cxlcache config failure.

echo Multi Process Test Start
check_privilege
check_movable_exist
check_cxlcache_exist
check_dir_fs_type
modify_cxlcache_to_enabled

if [ -z $RUN_ON_QEMU ]; then
	SIZE=("256m" "512m")
else
	SIZE=("16m" "32m")
fi

for i in "${SIZE[@]}";
do
	$CXLCACHE_TEST $i multi_process $TEST_DIR
	RETURN=$?
	if [ $RETURN -eq $ENV_SET_FAIL ]; then
		echo Environment setting for Test Fail.
		exit $ENV_SET_FAIL
	elif [ $RETURN -eq $TEST_FAILURE ]; then
		echo CXL Cache Test Fail.
		exit $TEST_FAILURE
	fi
done
echo Multi Process Test Succ.

exit $TEST_SUCCESS
############################################################################
