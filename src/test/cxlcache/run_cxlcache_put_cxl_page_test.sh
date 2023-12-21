#!/bin/bash

readonly TCDIR=$(readlink -f $(dirname $0))
source "$TCDIR/cxlcache_common.sh"

### prerequisite:
### 1. SMDK Kernel is running
### 2. CXL Cache must be enabled
### 3. Please run with root privilege.

################################# Run Test #################################
# 5. put_cxl_page test
# Description
# - Put the test file's data to CXL Cache.
# - Modify the page getting from CXL Cache and repeat putting and getting the page.
# - Check that the data getting from CXL Cache is same with modified contents.
# Pass/Fail
# - Pass : All data after page cache drop are same with before page cache drop.
# - Fail : Failure to meet the criteria for pass.
# - Error : mmap, file operations, sysfs setting, cxlcache config failure.

echo Put CXL Page Test Start
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

CXLNODE=`cat /proc/buddyinfo | grep Movable | awk '{print $2}'| head -n 1 | cut -d ',' -f1`
for i in "${SIZE[@]}";
do
    $CXLCACHE_TEST $i put_cxl_page $TEST_DIR $CXLNODE
    RETURN=$?
    if [ $RETURN -eq $ENV_SET_FAIL ]; then
        echo Environment setting for Test Fail.
        exit $ENV_SET_FAIL
    elif [ $RETURN -eq $TEST_FAILURE ]; then
        echo CXL Cache Test Fail.
        exit $TEST_FAILURE
    fi
done

echo Put CXL Test Succ.

exit $TEST_SUCCESS
############################################################################
