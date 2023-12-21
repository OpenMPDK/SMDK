#!/bin/bash

# Written by seungjun.ha@samsung.com at 2022 November
# Purpose for test CXL Swap Flush


# Cause CXL Swap is System Wide Feature,
# we cannot check the right value before and after flush.
# So we just show stored_pages in CXL Swap if CONFIG_DEBUG_FS is on.

SUCCESS_EXIT=0
FAILURE_EXIT=1
ENV_SET_FAIL=2

readonly BASEDIR=$(readlink -f $(dirname $0))/../../../

if [ ! -e $BASEDIR/script/common.sh ]; then
	echo common.sh Not Exist.
	echo Please locate TC under /path/to/dmdk.cxlmalloc/src/test/cxlswap/
	exit $ENV_SET_FAIL
fi
source "$BASEDIR/script/common.sh"

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
function modify_cxlswap_to_disabled() {
	if [ $CXLSWAP_STATUS = "Y" ]; then
		echo 0 > $CXLSWAPMODULE
		if [ $? -ne 0 ]; then
			echo Cannot modify CXL Swap Status to Disabled
			exit $ENV_SET_FAIL
		fi
	fi
}

################################# Run Test #################################
# 4. flush
# Description
# - Flush out CXL Swap's Stored_Pages to Disk if it can be.
# - Even after flush, it seems like there are few remain pages in CXL Swap.
# - These pages almost same value filled pages not interfere with N-Way Grouping.
# - But some pages are referenced under flush so cannot be flushed
#   which interfere with N-Way Grouping.
# - If you run flush repeatly, these pages can be flushed out to disk eventually.
# - Note that there's no CXL Swap's page in CXL memory, ENV_SET_FAIL is returned.
#   There's no problem to run flush itself in this case,
#   but this TC only targets flush out CXL Swap's pages in CXL memory.
#   So we return ENV_SET_FAIL in this case.
# Pass/Fail
# - Pass : Flush Operation Finished Well.
# - Fail/Error : Requirement not fulfilled.
#                - Cannot find loaded CXLSwap module.
#                - Cannot modify CXLSwap Status to Disabled (Maybe Not Sudo)
#                - There's no CXL Swap's page in CXL memory

echo Flush Test Start
check_privilege
check_cxlswap_exist
modify_cxlswap_to_disabled

DEBUGFS_EXIST="/sys/kernel/debug/cxlswap"
if [ -e $DEBUGFS_EXIST ]; then
	STORED_PAGES=$(cat /sys/kernel/debug/cxlswap/stored_pages)
	SAME_FILLED_PAGES=$(cat /sys/kernel/debug/cxlswap/same_filled_pages)
	ENV_CHECK=$(($STORED_PAGES-$SAME_FILLED_PAGES))
	if [ $ENV_CHECK -eq 0 ]; then
		echo "There's no CXL Swap's page in CXL memory"
		exit $ENV_SET_FAIL
	fi
	echo Before Flush : ${ENV_CHECK}
fi
echo 1 > /sys/module/cxlswap/parameters/flush
if [ $? -eq 0 ]; then
	if [ -e $DEBUGFS_EXIST ]; then
		STORED_PAGES=$(cat /sys/kernel/debug/cxlswap/stored_pages)
		SAME_FILLED_PAGES=$(cat /sys/kernel/debug/cxlswap/same_filled_pages)
		ENV_CHECK=$(($STORED_PAGES-$SAME_FILLED_PAGES))
		echo After Flush : ${ENV_CHECK}
	fi
	echo Flush Test Finish
	exit $SUCCESS_EXIT
else
	echo Check Printed Message.
	exit $FAILURE_EXIT
fi
############################################################################
