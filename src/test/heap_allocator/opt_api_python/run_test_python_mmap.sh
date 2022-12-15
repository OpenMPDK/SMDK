#!/bin/bash

PYTHON=python3
readonly BASEDIR=$(readlink -f $(dirname $0))/../../../../
SMALLOCLIB_DIR=$BASEDIR/lib/smdk_allocator/lib
SMALLOCPYLIB_DIR=$BASEDIR/lib/smdk_allocator/opt_api/py_smdk_pkg
SMALLOCPYLIB=$SMALLOCPYLIB_DIR/_py_smdk.so

SCRIPT_PATH=$(readlink -f $(dirname $0))/
APP=$SCRIPT_PATH/test_python_mmap.py
FILE=$SCRIPT_PATH/4M.dummy

### Note:
### make dummy file to run test
if [ ! -e $FILE ];then
    dd if=/dev/zero of=$FILE bs=1M count=4
fi
### If _py_smdk.so is linked dynamically with libsmalloc.so(not libsmalloc.a), below cmd is required.
export LD_LIBRARY_PATH=$SMALLOCLIB_DIR
### If _py_smdk.so is linked with TLS enabled libsmalloc(jemalloc), below cmd is required.
export LD_PRELOAD=$SMALLOCPYLIB
### If _py_smdk.so is not copied in the basic path of python package, PYTHONPATH should be specified as below.
export PYTHONPATH=$SMALLOCPYLIB_DIR

MEMTYPE=normal

function usage(){
    echo "Usage: $0 [-e | -n]"
    exit 2
}

MEM_SET=0

while getopts "en" opt; do
    case "$opt" in
        e)
            if [ $MEM_SET == 0 ]; then
                MEMTYPE=exmem
                MEM_SET=1
            fi
            ;;
        n)
            if [ $MEM_SET == 0 ]; then
                MEMTYPE=normal
                MEM_SET=1
            fi
            ;;
        *)
            unset LD_PRELOAD
            usage
            ;;
    esac
done

$PYTHON $APP $MEMTYPE
ret=$?

unset LD_PRELOAD

if [ -e $FILE ];then
    rm -rf $FILE
fi

echo
if [ $ret == 0 ]; then
    echo "PASS"
else
    echo "FAIL"
    exit 1
fi

exit 0
