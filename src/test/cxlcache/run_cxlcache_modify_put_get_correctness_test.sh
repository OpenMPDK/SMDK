#!/bin/bash

readonly TCDIR=$(readlink -f $(dirname $0))
source "$TCDIR/cxlcache_common.sh"

### prerequisite:
### 1. SMDK Kernel is running
### 2. CXL Cache must be enabled
### 3. Please run with root privilege.

################################# Run Test #################################
# 2. modify_put_get_correctness test
# Description
# - The page is putting at CXL Cache by page cache drop and getting the page from CXL Cache.
# - Modify the page getting from CXL Cache and repeat putting and getting the page.
# - Check that the data getting from CXL Cache is same with modified contents.
# Pass/Fail
# - Pass : All data after page cache drop are same with before page cache drop.
# - Fail : Failure to meet the criteria for pass.
# - Error : mmap, file operations, cxlcache config failure.

echo Modify Put Get Correctness Test Start
check_privilege
check_exmem_exist
check_cxlcache_exist
check_dir_fs_type
modify_cxlcache_to_enabled

for i in 256m 512m 1g;
do
	$CXLCACHE_TEST $i modify_put_get_correctness $TEST_DIR
	RETURN=$?
	if [ $RETURN -eq $ENV_SET_FAIL ]; then
		echo Environment setting for Test Fail.
		exit $ENV_SET_FAIL
	elif [ $RETURN -eq $TEST_FAILURE ]; then
		echo CXL Cache Test Fail.
		exit $TEST_FAILURE
	fi
done
echo Modify Put Get Correctness Test Succ.

exit $TEST_SUCCESS
############################################################################
