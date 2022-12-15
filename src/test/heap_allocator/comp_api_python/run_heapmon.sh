#!/usr/bin/env bash
readonly BASEDIR=$(readlink -f $(dirname $0))/../../../../
CXLMALLOCDIR=$BASEDIR/lib/smdk_allocator/lib

SCRIPT_PATH=$(readlink -f $(dirname $0))/

PRIORITY=normal
PYTHON=python3

function run_app(){
    unset LD_PRELOAD
    CXLMALLOC=$CXLMALLOCDIR/libcxlmalloc.so
    CXLMALLOC_CONF=use_exmem:true,exmem_zone_size:16384,normal_zone_size:16384,maxmemory_policy:remain
    export LD_PRELOAD=$CXLMALLOC
    if [ "$PRIORITY" == 'exmem' ]; then
        CXLMALLOC_CONF+=,priority:exmem
    elif [ "$PRIORITY" == 'normal' ]; then
        CXLMALLOC_CONF+=,priority:normal
    fi

    export CXLMALLOC_CONF
    echo $CXLMALLOC_CONF
    $PYTHON -O $SCRIPT_PATH/heapmon.py
}

function run_libc(){
    unset LD_PRELOAD
    $PYTHON $SCRIPT_PATH/heapmon.py
}

function usage(){
    echo "Usage: $0 [-e | -n] [-l | -a]"
    exit 2
}

MEM_SET=0
APP=0

while getopts "enla" opt; do
    case "$opt" in
        e)
            if [ $MEM_SET == 0 ]; then
                PRIORITY='exmem'
                MEM_SET=1
            fi
            ;;
        n)
            if [ $MEM_SET == 0 ]; then
                PRIORITY='normal'
                MEM_SET=1
            fi
            ;;
        l)
            if [ $APP == 0 ]; then
                APP="libc"
            fi
            ;;
        a)
            if [ $APP == 0 ]; then
                APP="app"
            fi
            ;;
        *)
            usage
            ;;
    esac
done

case "$APP" in
    libc)
        run_libc
        ret=$?
        ;;
    app)
        run_app
        ret=$?
        ;;
    *)
        usage
        ;;
esac

echo
if [ $ret == 0 ]; then
    echo "PASS"
else
    echo "FAIL"
    exit 1
fi

exit 0
