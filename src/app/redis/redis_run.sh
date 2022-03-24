#!/usr/bin/env bash
# prerequisite: redis installation
#set -x
readonly BASEDIR=$(readlink -f $(dirname $0))/../../../
REDIS=redis-6.2.1
REDISDIR=$BASEDIR/lib/$REDIS/src/
REDISCONF=$BASEDIR/src/app/redis/redis.summary.conf

PRIORITY=exmem
NUMACTL=""

function run_app(){
	rm *rdb *aof 2>/dev/null
	echo 3 > /proc/sys/vm/drop_caches

	unset LD_PRELOAD
	CXLMALLOC=$BASEDIR/lib/smdk_allocator/lib/libcxlmalloc.so
	export LD_PRELOAD=$CXLMALLOC

	CXLMALLOC_CONF=use_exmem:true,exmem_zone_size:16384,normal_zone_size:16384,maxmemory_policy:remain
	if [ "$PRIORITY" == 'exmem' ]; then
		CXLMALLOC_CONF+=,priority:exmem,:
		echo $CXLMALLOC_CONF
		export CXLMALLOC_CONF
	elif [ "$PRIORITY" == 'normal' ]; then
		CXLMALLOC_CONF+=,priority:normal,:
		echo $CXLMALLOC_CONF
		export CXLMALLOC_CONF
	fi
	$NUMACTL $REDISDIR/redis-server $REDISCONF --loglevel verbose

	# --loglevel : debug / verbose / notice / warning
	# gdb --args $1 /redis.conf --loglevel debug
	# ./redis-server --test-memory <megabytes>
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
			PRIORITY="exmem"
			;;
		n)
			PRIORITY="normal"
			;;
		*)
			echo "Usage: $0 -a <node 0 or 1> -e[exmem] | -n[normal]"
			echo "Usage: $0 -a 0 -e or $0 -a 1 -n"
			exit 1
			;;
	esac
done
run_app

# Run app
