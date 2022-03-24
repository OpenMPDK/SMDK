# prerequisite:
# 1. SMDK Kernel is running
# 2. $BASEDIR/lib/build_lib.sh numactl
#!/usr/bin/env bash
readonly BASEDIR=$(readlink -f $(dirname $0))/../../../
source "$BASEDIR/script/common.sh"

# numactl
NUMACTL_DIR=$BASEDIR/lib/numactl-2.0.14/
NUMACTL=$NUMACTL_DIR/numactl

# TC
TEST_APP_DIR=$BASEDIR/src/test/subzone/
TEST_APP=$TEST_APP_DIR/test_random_malloc

# Run test app with --zone e option
$NUMACTL -z e -i all $TEST_APP
