#!/usr/bin/env bash

readonly BASE_DIR=$(readlink -f $(dirname $0))/../../../..
CXLMALLOC_DIR=$BASE_DIR/lib/smdk_allocator/lib
echo $CXLMALLOC_DIR

SCRIPT_PATH=$(readlink -f $(dirname $0))/
APP=$SCRIPT_PATH/test

PRIORITY=normal

function run_app(){
    unset LD_PRELOAD
    export LD_PRELOAD=$CXLMALLOC_DIR/libcxlmalloc.so
    export CXLMALLOC_CONF=use_exmem:true,use_auto_arena_scaling:true,normal_zone_size:2048,exmem_zone_size:2048,maxmemory_policy:interleave
    if [ "$PRIORITY" == 'exmem' ]; then
        CXLMALLOC_CONF+=,priority:exmem
    elif [ "$PRIORITY" == 'normal' ]; then
        CXLMALLOC_CONF+=,priority:normal
    fi
    export CXLMALLOC_CONF
    echo $CXLMALLOC_CONF
    $APP
}

function usage(){
    echo "Usage: $0 [-e | -n]"
    exit 2
}

MEM_SET=0

while getopts "en" opt; do
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
        *)
            usage
            ;;
    esac
done

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
