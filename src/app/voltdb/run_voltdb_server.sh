#!/usr/bin/env bash
readonly BASEDIR=$(readlink -f $(dirname $0))/../../../

PRIORITY=normal

function run_app(){
    cd $BASEDIR/src/app/voltdb/voltdb_src/bin
    ./voltdb init

    unset LD_PRELOAD
    CXLMALLOC=$BASEDIR/lib/smdk_allocator/lib/libcxlmalloc.so
    export LD_PRELOAD=$CXLMALLOC
    CXLMALLOC_CONF=use_exmem:true,exmem_zone_size:16384,normal_zone_size:16384,maxmemory_policy:remain
    if [ "$PRIORITY" == 'exmem' ]; then
        CXLMALLOC_CONF+=,priority:exmem,:
    elif [ "$PRIORITY" == 'normal' ]; then
        CXLMALLOC_CONF+=,priority:normal,:
    fi

    export CXLMALLOC_CONF
    echo $CXLMALLOC_CONF

    ./voltdb start
}

while getopts ":ena" opt; do
    case "$opt" in
        e)
            PRIORITY='exmem'
            ;;
        n)
            PRIORITY='normal'
            ;;
        a)
            run_app
            ;;
        :)
            echo "Usage: $0 [-e | -n] -a"
    esac
done
