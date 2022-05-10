#!/usr/bin/env bash
# prerequisite : install jemalloc
readonly BASEDIR=$(readlink -f $(dirname $0))/../../../../
CXLMALLOCDIR=$BASEDIR/lib/smdk_allocator/lib
source "$BASEDIR/script/common.sh"
export PATH=$PATH:.
APP=test_heap_alloc

function test_cxlmalloc_exmem(){
	log_normal "cxlmalloc - $APP"
	export LD_PRELOAD=$CXLMALLOCDIR/libcxlmalloc.so
	export CXLMALLOC_CONF=use_exmem:true,use_auto_arena_scaling:true,priority:exmem,exmem_zone_size:1000,normal_zone_size:1000,maxmemory_policy:interleave,exmem_partition_range:2,3
	echo $CXLMALLOC_CONF
	$APP
}

function test_cxlmalloc_normal(){
	log_normal "cxlmalloc - $APP"
	export LD_PRELOAD=$CXLMALLOCDIR/libcxlmalloc.so
	export CXLMALLOC_CONF=use_exmem:true,use_auto_arena_scaling:true,priority:normal,exmem_zone_size:1000,normal_zone_size:1000,maxmemory_policy:interleave
	echo $CXLMALLOC_CONF
	$APP
}

while getopts "en" opt; do
	case "$opt" in
		e)
			test_cxlmalloc_exmem
			;;
		n)
			test_cxlmalloc_normal
			;;
		*)
			echo "Usage: $0 -e | -n"
			;;
	esac
done
exit 0
