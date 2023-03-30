#!/bin/bash

readonly TCDIR=$(readlink -f $(dirname $0))
source "$TCDIR/cxlcache_common.sh"

### prerequisite:
### 1. SMDK Kernel is running
### 2. CXL Cache must be enabled
### 3. Please run with root privilege.

################################# Run Test #################################
# 6. flush
# Description
# - Flush out CXL Cache's Put_Pages.
# - Note that there's no CXL Cache's page in ZONE_EXMEM, ENV_SET_FAIL is returned.
#   There's no problem to run flush itself in this case,
#   but this TC only targets flush out CXL Cache's pages in ZONE_EXMEM.
#   So we return ENV_SET_FAIL in this case.
# Pass/Fail
# - Pass : Flush Operation Finished Well.
# - Fail/Error : Requirement not fulfilled.
#                - Cannot find loaded CXLCache module.
#                - Cannot modify CXLCache Status to Disabled (Maybe Not Sudo)
#                - There's no CXL Cache's page in ZONE_EXMEM

echo Flush Test Start
check_privilege
check_exmem_exist
check_cxlcache_exist
modify_cxlcache_to_disabled

DEBUGFS_EXIST="/sys/kernel/debug/cxlcache"
if [ -e $DEBUGFS_EXIST ]; then
	PUT_PAGES=$(cat /sys/kernel/debug/cxlcache/put_pages)
	if [ $PUT_PAGES -eq 0 ]; then
		echo "There's no CXL Cache's page in ZONE_EXMEM"
		exit $ENV_SET_FAIL
	fi
	echo Before Flush : $PUT_PAGES
fi
echo 1 > /sys/module/cxlcache/parameters/flush
if [ $? -eq 0 ]; then
	if [ -e $DEBUGFS_EXIST ]; then
		PUT_PAGES=$(cat /sys/kernel/debug/cxlcache/put_pages)
		echo After Flush : $PUT_PAGES
	fi
	echo Flush Test Finish
	exit $TEST_SUCCESS
else
	echo Check Printed Message.
	exit $TEST_FAILURE
fi
############################################################################
