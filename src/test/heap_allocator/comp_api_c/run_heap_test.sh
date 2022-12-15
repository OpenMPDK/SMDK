#!/usr/bin/env bash
# prerequisite : install jemalloc
readonly BASEDIR=$(readlink -f $(dirname $0))/../../../../
CXLMALLOCDIR=$BASEDIR/lib/smdk_allocator/lib
source "$BASEDIR/script/common.sh"

SCRIPT_PATH=$(readlink -f $(dirname $0))/
APP=$SCRIPT_PATH/test_heap_alloc

function test_cxlmalloc_exmem(){
    log_normal "EXMEM Zone - cxlmalloc - $APP"
    export LD_PRELOAD=$CXLMALLOCDIR/libcxlmalloc.so
    export CXLMALLOC_CONF=use_exmem:true,use_auto_arena_scaling:true,priority:exmem,exmem_zone_size:1000,normal_zone_size:1000,maxmemory_policy:interleave
    echo $CXLMALLOC_CONF
    $APP
}

function test_cxlmalloc_normal(){
    log_normal "NORMAL Zone - cxlmalloc - $APP"
    export LD_PRELOAD=$CXLMALLOCDIR/libcxlmalloc.so
    export CXLMALLOC_CONF=use_exmem:true,use_auto_arena_scaling:true,priority:normal,exmem_zone_size:1000,normal_zone_size:1000,maxmemory_policy:interleave
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
                test_cxlmalloc_exmem
                ret=$?
                MEM_SET=1
            fi
            ;;
        n)
            if [ $MEM_SET == 0 ]; then
                test_cxlmalloc_normal
                ret=$?
                MEM_SET=1
            fi
            ;;
        *)
            usage
            ;;
    esac
done

if [ $MEM_SET == 0 ]; then
    usage
fi

echo
if [ $ret == 0 ]; then
    echo "PASS"
else
    echo "FAIL"
    exit 1
fi

exit 0
