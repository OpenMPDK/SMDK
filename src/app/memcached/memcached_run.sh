#!/usr/bin/env bash
# prerequisite: memcached installation
readonly BASEDIR=$(readlink -f $(dirname $0))/../../../
MEMCACHED=memcached-1.6.9
MEMCACHEDDIR=$BASEDIR/lib/$MEMCACHED/
CXLMALLOCDIR=$BASEDIR/lib/smdk_allocator/lib/

EXTFILE=/opt/memcache_file
EXTSIZE=20g
OPTEXT="-o ext_page_size=2,ext_wbuf_size=2,ext_path=$EXTFILE:$EXTSIZE,ext_item_age=1,ext_threads=1,ext_compact_under=1000,ext_drop_under=1000,ext_direct_io=1"
#OPTEXT="-o ext_page_size=8,ext_wbuf_size=2,ext_path=/memcache_file:64m,ext_threads=1,ext_io_depth=2,ext_item_size=512,ext_item_age=2,ext_recache_rate=10000,ext_max_frag=0.9,slab_automove=0,ext_compact_under=1"
#OPTEXT="-o ext_path=/memcache_file:256m,ext_item_age=2"

PRIORITY=exmem
TASKSET=""


function run_app(){
	rm $EXTFILE* 2>/dev/null
	sudo echo 3 > /proc/sys/vm/drop_caches
	echo $CXLMALLOC_CONF

	unset LD_PRELOAD
	export LD_PRELOAD=$CXLMALLOCDIR/libcxlmalloc.so
	CXLMALLOC_CONF=use_exmem:true,exmem_size:16384,normal_size:16384,maxmemory_policy:remain
	if [ "$PRIORITY" == 'exmem' ]; then
		CXLMALLOC_CONF+=,priority:exmem
	elif [ "$PRIORITY" == 'normal' ]; then
		CXLMALLOC_CONF+=,priority:normal
	fi
	echo $CXLMALLOC_CONF
	export CXLMALLOC_CONF
	$TASKSET $MEMCACHEDDIR/memcached -m 4096 -c 1400 -u root #$OPTEXT #-vv
}

while getopts "a:en" opt; do
	case "$opt" in
		a)
			NODENUM=$OPTARG
			if [ $NODENUM -eq 0 ]; then
				TASKSET="numactl -N 0"
			elif [ $NODENUM -eq 1 ]; then
				TASKSET="numactl -N 1"
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
			echo "Usage: $0 -a 0 -e  or $0 -a 1 -n"
			exit 1
			;;
	esac
done
run_app
