#!/bin/bash

PYTHON=python3
readonly BASEDIR=$(readlink -f $(dirname $0))/../../../../
source "$BASEDIR/script/common.sh"

SMALLOCLIB_DIR=$BASEDIR/lib/smdk_allocator/lib
SMALLOCPYLIB_DIR=$BASEDIR/lib/smdk_allocator/opt_api/py_smdk_pkg
SMALLOCPYLIB=$SMALLOCPYLIB_DIR/_py_smdk.so

SCRIPT_PATH=$(readlink -f $(dirname $0))/
APP=$SCRIPT_PATH/test_opt_api_py.py

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
}

### Note:
### If _py_smdk.so is linked dynamically with libsmalloc.so(not libsmalloc.a), below cmd is required.
export LD_LIBRARY_PATH=$SMALLOCLIB_DIR
### If _py_smdk.so is not copied in the basic path of python package, PYTHONPATH should be specified as below.
export PYTHONPATH=$SMALLOCPYLIB_DIR

get_cxl_node
$PYTHON $APP $CXLNODE
ret=$?
unset LD_PRELOAD

echo
if [ $ret == 0 ]; then
    echo "PASS"
else
    echo "FAIL"
    exit 1
fi

exit 0

