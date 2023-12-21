#!/usr/bin/env bash
# prerequisite:
# 1. SMDK Kernel is running
# 2. $BASEDIR/lib/build_lib.sh numactl

readonly BASEDIR=$(readlink -f $(dirname $0))/../../../
source "$BASEDIR/script/common.sh"

# numactl
NUMACTL_DIR=$BASEDIR/lib/numactl-2.0.16/
NUMACTL=$NUMACTL_DIR/numactl

# TC
TEST_APP_DIR=$BASEDIR/src/test/subzone/
TEST_APP=$TEST_APP_DIR/test_random_malloc

if [ ! -f "${NUMACTL}" ]; then
    log_error "numactl does not exist. Run 'build_lib.sh numactl' in /path/to/SMDK/lib/"
    exit 2
fi

if [ ! -f "${TEST_APP}" ]; then
    log_error "test application does not exist. Run 'make' in test directory"
    exit 2
fi

get_cxl_nodes() {
    MOVABLES=`cat /proc/buddyinfo | grep Movable | awk '{ printf $2 }' | sed 's/.$//'`
    IFS=',' read -ra tokens <<< "$MOVABLES"
    for nid in "${tokens[@]}"; do
        dirs=($(find /sys/devices/system/node/node$nid -maxdepth 1 -type l -name "cpu*"))
        if [ ${#dirs[@]} -eq 0 ]; then
            if [ -z "$CXLNODES" ]; then
                CXLNODES=$nid
            else
                CXLNODES+=",$nid"
            fi
        fi
    done

    if [ -z "$CXLNODES" ]; then
        log_error "cxl nodes don't exist."
        exit 2
    fi
}

get_cxl_nodes

# Run test app
$NUMACTL -i $CXLNODES $TEST_APP
ret=$?

echo
if [ $ret == 0 ]; then
	echo "PASS"
else
	echo "FAIL"
	exit 1
fi

exit 0

