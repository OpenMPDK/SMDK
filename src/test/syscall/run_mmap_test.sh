#!/usr/bin/env bash

readonly BASEDIR=$(readlink -f $(dirname $0))/../../../

function run_app(){
	unset LD_PRELOAD
	CXLMALLOC_DIR=$BASEDIR/lib/smdk_allocator/lib/libcxlmalloc.so
	export LD_PRELOAD=$CXLMALLOC_DIR
	CXLMALLOC_CONF=use_exmem:true,exmem_zone_size:4096,normal_zone_size:4096,maxmemory_policy:interleave
        if [ "$PRIORITY" == 'exmem' ]; then
                CXLMALLOC_CONF+=,priority:exmem
        elif [ "$PRIORITY" == 'normal' ]; then
                CXLMALLOC_CONF+=,priority:normal
        fi
	echo $CXLMALLOC_CONF
	export CXLMALLOC_CONF
	./test_syscall
}

while getopts "en" opt; do
        case "$opt" in
                e)
                        PRIORITY="exmem"
                        run_app
                        ;;
                n)
                        PRIORITY="normal"
                        run_app
                        ;;
                :)
                        echo "Usage: $0 -e[exmem] | -n[normal]"
                        echo "Usage: $0 -e or $0 -n"
                        exit 1
                        ;;
        esac
done
