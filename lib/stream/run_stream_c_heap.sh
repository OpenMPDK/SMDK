#!/usr/bin/env bash
# prerequisite: memcached installation
readonly BASEDIR=$(readlink -f $(dirname $0))/../../
STREAM=stream_c_heap.exe

PRIORITY=exmem
NUMACTL=""

function run_app(){
	echo 3 > /proc/sys/vm/drop_caches
	echo $CXLMALLOC_CONF

	unset LD_PRELOAD
	CXLMALLOCDIR=$BASEDIR/lib/smdk_allocator/lib
	if [ ! -f "$CXLMALLOCDIR/libcxlmalloc.so" ]; then
		echo "Error: libcxlmalloc.so: no such file"
		echo ""
		exit 1
	fi
	export LD_PRELOAD=$CXLMALLOCDIR/libcxlmalloc.so
	CXLMALLOC_CONF=use_exmem:true,exmem_size:32768,normal_size:32768,maxmemory_policy:remain
	if [ "$PRIORITY" == 'exmem' ]; then
		CXLMALLOC_CONF+=,priority:exmem,:
	elif [ "$PRIORITY" == 'normal' ]; then
		CXLMALLOC_CONF+=,priority:normal,:
	fi
	echo $CXLMALLOC_CONF
	export CXLMALLOC_CONF
	$NUMACTL $BASEDIR/lib/stream/$STREAM
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
