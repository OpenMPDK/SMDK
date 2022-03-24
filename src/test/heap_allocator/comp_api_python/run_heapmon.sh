#!/usr/bin/env bash
readonly BASEDIR=$(readlink -f $(dirname $0))/../../../../
CXLMALLOCDIR=$BASEDIR/lib/smdk_allocator/lib

PRIORITY=normal
PYTHON=python3

function run_app(){
	unset LD_PRELOAD
	CXLMALLOC=$CXLMALLOCDIR/libcxlmalloc.so
	CXLMALLOC_CONF=use_exmem:true,exmem_zone_size:16384,normal_zone_size:16384,maxmemory_policy:remain
	export LD_PRELOAD=$CXLMALLOC
	if [ "$PRIORITY" == 'exmem' ]; then
		CXLMALLOC_CONF+=,priority:exmem,:
		export CXLMALLOC_CONF
		echo $CXLMALLOC_CONF
		$PYTHON -O ./heapmon.py
	elif [ "$PRIORITY" == 'normal' ]; then
		CXLMALLOC_CONF+=,priority:normal,:
		export CXLMALLOC_CONF
		echo $CXLMALLOC_CONF
		$PYTHON -O ./heapmon.py
	fi
}

function run_libc(){
	unset LD_PRELOAD
	$PYTHON ./heapmon.py
}

function run_pytest(){
	unset LD_PRELOAD
	CXLMALLOC=$CXLMALLOCDIR/libcxlmalloc.so
	CXLMALLOC_CONF=use_exmem:true,exmem_zone_size:16384,normal_zone_size:16384,maxmemory_policy:remain
	export LD_PRELOAD=$CXLMALLOC
	if [ "$PRIORITY" == 'exmem' ]; then
		CXLMALLOC_CONF+=,priority:exmem,:
		export CXLMALLOC_CONF
		echo $CXLMALLOC_CONF
		$PYTHON -C -m test --pgo || true
	elif [ "$PRIORITY" == 'normal' ]; then
		CXLMALLOC_CONF+=,priority:normal,:
		export CXLMALLOC_CONF
		echo $CXLMALLOC_CONF
		$PYTHON -m test --pgo || true
	fi
	cd -
}

while getopts "enlap" opt; do
	case "$opt" in
		e)
			PRIORITY='exmem'
			;;
		n)
			PRIORITY='normal'
			;;
		l)
			run_libc
			;;
		a)
			run_app
			;;
		p)
			run_pytest
			;;
		*)
			echo "Usage: $0 -e -a | -n -a"
			echo "Usage: $0 -e -p | -n -p"
			echo "Usage: $0 -e -l | -n -l"
	esac

done
