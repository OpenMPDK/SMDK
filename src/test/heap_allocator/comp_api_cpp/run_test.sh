#!/usr/bin/env bash

readonly BASE_DIR=$(readlink -f $(dirname $0))/../../../..
CXLMALLOC_DIR=$BASE_DIR/lib/smdk_allocator/lib
echo $CXLMALLOC_DIR

PRIORITY=normal
ARGS=""

function run_app(){
	unset LD_PRELOAD
	export LD_PRELOAD=$CXLMALLOC_DIR/libcxlmalloc.so
	export CXLMALLOC_CONF=use_exmem:true,use_auto_arena_scaling:true,normal_zone_size:2048,exmem_zone_size:2048,maxmemory_policy:interleave
        if [ "$PRIORITY" == 'exmem' ]; then
                CXLMALLOC_CONF+=,priority:exmem
                export CXLMALLOC_CONF
                echo $CXLMALLOC_CONF
        elif [ "$PRIORITY" == 'normal' ]; then
                CXLMALLOC_CONF+=,priority:normal
                export CXLMALLOC_CONF
                echo $CXLMALLOC_CONF
	fi
	./test
}

ARGS=$@
run_app

while getopts "en" opt; do
        case "$opt" in
                e)
                        PRIORITY='exmem'
			run_app
                        ;;
                n)
                        PRIORITY='normal'
			run_app
                        ;;
                *)
                        echo "Usage: $0 -e | -n"
        esac

done

