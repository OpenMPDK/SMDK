#!/usr/bin/env bash

readonly BASEDIR=$(readlink -f $(dirname $0))/../../
CXLMALLOC=$BASEDIR/lib/smdk_allocator/lib/libcxlmalloc.so
MLC=$BASEDIR/lib/mlc/Linux/mlc

PRIORITY='normal'
NUMACTL=""

function run_app(){
    echo 3 > /proc/sys/vm/drop_caches

    unset LD_PRELOAD
    export LD_PRELOAD=$CXLMALLOC
    CXLMALLOC_CONF=use_exmem:true,exmem_zone_size:65536,normal_zone_size:65536,maxmemory_policy:remain
    if [ "$PRIORITY" == 'exmem' ]; then
        CXLMALLOC_CONF+=,priority:exmem,:
    elif [ "$PRIORITY" == 'normal' ]; then
        CXLMALLOC_CONF+=,priority:normal,:
    fi
    echo $CXLMALLOC_CONF
    export CXLMALLOC_CONF
    $NUMACTL $MLC
}


while getopts "a:en" opt; do
	case "$opt" in
		a)
			NODENUM=$OPTARG
			if [ $NODENUM -eq 0 ]; then
				NUMACTL="numactl -N 0"
			elif [ $NODENUM -eq 1 ]; then
				NUMACTL="numactl -N 1"
			else
				echo "Node number should be 0 or 1"
				exit 1
			fi
			;;
		e)
			PRIORITY='exmem'
			;;
		n)
			PRIORITY='normal'
			;;
		*)
			echo "Usage: $0 -a <node 0 or 1> -e[exmem] -n[normal]"
			echo "Usage: $0 -a 0 -e or $0 -a 1 -n"
			exit 1
			;;
	esac
done
run_app
