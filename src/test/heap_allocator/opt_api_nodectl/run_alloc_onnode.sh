#!/usr/bin/env bash
readonly BASEDIR=$(readlink -f $(dirname $0))/../../../../
source "$BASEDIR/script/common.sh"

ARGS=$@
if [ ! -z $RUN_ON_QEMU ]; then
	ARGS+=" size 33554432"
fi

SCRIPT_PATH=$(readlink -f $(dirname $0))/
APP=$SCRIPT_PATH/test_alloc_onnode

get_cxl_node() {
    MOVABLES=`cat /proc/buddyinfo | grep Movable | awk '{ printf $2 }' | sed 's/.$//'`
    IFS=',' read -ra tokens <<< "$MOVABLES"
    for nid in "${tokens[@]}"; do
        dirs=($(find /sys/devices/system/node/node$nid -maxdepth 1 -type l -name "cpu*"))
        if [ ${#dirs[@]} -eq 0 ]; then
            if [ -z "$CXLNODE" ]; then
                CXLNODE=$nid
                break
            fi
        fi
    done

    if [ -z "$CXLNODE" ]; then
        log_error "cxl node doesn't exist."
        exit 2
    fi

    ARGS+=" node $CXLNODE"
}

function run_app(){
## dynamic link
        export LD_LIBRARY_PATH=$BASEDIR/lib/smdk_allocator/lib/
## common
        SMALLOC_CONF=use_auto_arena_scaling:true
#       echo $SMALLOC_CONF
        export SMALLOC_CONF

        $APP $ARGS
}

get_cxl_node
run_app
ret=$?

echo
if [ $ret == 0 ]; then
    echo "PASS"
else
    echo "FAIL"
    exit 1
fi

exit 0

