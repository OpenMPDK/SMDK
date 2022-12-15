#!/usr/bin/env bash
# prerequisite:
# 1. SMDK Kernel is running
# 2. $BASEDIR/lib/build_lib.sh numactl

readonly BASEDIR=$(readlink -f $(dirname $0))/../../../
source "$BASEDIR/script/common.sh"

# numactl
NUMACTL_DIR=$BASEDIR/lib/numactl-2.0.14/
NUMACTL=$NUMACTL_DIR/numactl

# TC
TEST_APP_DIR=$BASEDIR/src/test/subzone/
TEST_APP=$TEST_APP_DIR/test_random_malloc

if [ ! -f "${NUMACTL}" ]; then
    log_error "numactl does not exist. Run 'build_lib.sh numactl' in /path/to/SMDK/lib/"
    exit 2
fi

# Run test app with --zone e option
$NUMACTL -z e -i all $TEST_APP
ret=$?

echo
if [ $ret == 0 ]; then
	echo "PASS"
else
	echo "FAIL"
	exit 1
fi

exit 0

