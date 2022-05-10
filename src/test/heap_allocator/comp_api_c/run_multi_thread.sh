#!/usr/bin/env bash
# prerequisite : install jemalloc
readonly BASEDIR=$(readlink -f $(dirname $0))/../../../../
CXLMALLOCDIR=$BASEDIR/lib/smdk_allocator/lib

source "$BASEDIR/script/common.sh"

APP=./test_multi_thread
config=$@

	export LD_PRELOAD=$CXLMALLOCDIR/libcxlmalloc.so

	export CXLMALLOC_CONF=use_exmem:true,use_auto_arena_scaling:true,priority:exmem,exmem_zone_size:2G,normal_zone_size:2G,maxmemory_policy:interleave
	$APP $config
	export CXLMALLOC_CONF=use_exmem:true,use_auto_arena_scaling:true,priority:exmem,exmem_zone_size:2G,normal_zone_size:2G,maxmemory_policy:oom
	$APP $config
	export CXLMALLOC_CONF=use_exmem:true,use_auto_arena_scaling:true,priority:exmem,exmem_zone_size:2G,normal_zone_size:2G,maxmemory_policy:remain
	$APP $config
	export CXLMALLOC_CONF=use_exmem:true,use_auto_arena_scaling:true,priority:normal,exmem_zone_size:2G,normal_zone_size:2G,maxmemory_policy:interleave
	$APP $config
	export CXLMALLOC_CONF=use_exmem:true,use_auto_arena_scaling:true,priority:normal,exmem_zone_size:2G,normal_zone_size:2G,maxmemory_policy:oom
	$APP $config
	export CXLMALLOC_CONF=use_exmem:true,use_auto_arena_scaling:true,priority:normal,exmem_zone_size:2G,normal_zone_size:2G,maxmemory_policy:remain
	$APP $config
	export CXLMALLOC_CONF=use_exmem:true,use_auto_arena_scaling:false,priority:exmem,exmem_zone_size:2G,normal_zone_size:2G,maxmemory_policy:interleave
	$APP $config
	export CXLMALLOC_CONF=use_exmem:true,use_auto_arena_scaling:false,priority:exmem,exmem_zone_size:2G,normal_zone_size:2G,maxmemory_policy:oom
	$APP $config
	export CXLMALLOC_CONF=use_exmem:true,use_auto_arena_scaling:false,priority:exmem,exmem_zone_size:2G,normal_zone_size:2G,maxmemory_policy:remain
	$APP $config
	export CXLMALLOC_CONF=use_exmem:true,use_auto_arena_scaling:false,priority:normal,exmem_zone_size:2G,normal_zone_size:2G,maxmemory_policy:interleave
	$APP $config
	export CXLMALLOC_CONF=use_exmem:true,use_auto_arena_scaling:false,priority:normal,exmem_zone_size:2G,normal_zone_size:2G,maxmemory_policy:oom
	$APP $config
	export CXLMALLOC_CONF=use_exmem:true,use_auto_arena_scaling:false,priority:normal,exmem_zone_size:2G,normal_zone_size:2G,maxmemory_policy:remain
	$APP $config
	export CXLMALLOC_CONF=use_exmem:true,use_auto_arena_scaling:true,priority:exmem,exmem_zone_size:2G,normal_zone_size:2G,maxmemory_policy:interleave,exmem_partition_range:all
	$APP $config
	export CXLMALLOC_CONF=use_exmem:true,use_auto_arena_scaling:true,priority:exmem,exmem_zone_size:2G,normal_zone_size:2G,maxmemory_policy:oom,exmem_partition_range:0
	$APP $config
	export CXLMALLOC_CONF=use_exmem:true,use_auto_arena_scaling:true,priority:exmem,exmem_zone_size:2G,normal_zone_size:2G,maxmemory_policy:remain,exmem_partition_range:1
	$APP $config
	export CXLMALLOC_CONF=use_exmem:true,use_auto_arena_scaling:true,priority:normal,exmem_zone_size:2G,normal_zone_size:2G,maxmemory_policy:interleave,exmem_partition_range:0,1
	$APP $config
	export CXLMALLOC_CONF=use_exmem:true,use_auto_arena_scaling:true,priority:normal,exmem_zone_size:2G,normal_zone_size:2G,maxmemory_policy:oom,exmem_partition_range:0-2
	$APP $config
	export CXLMALLOC_CONF=use_exmem:true,use_auto_arena_scaling:true,priority:normal,exmem_zone_size:2G,normal_zone_size:2G,maxmemory_policy:remain,exmem_partition_range:0,2-3
	$APP $config
exit 0
