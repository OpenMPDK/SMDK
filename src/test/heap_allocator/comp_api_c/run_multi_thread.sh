#!/usr/bin/env bash
# prerequisite : install jemalloc
readonly BASEDIR=$(readlink -f $(dirname $0))/../../../../
CXLMALLOCDIR=$BASEDIR/lib/smdk_allocator/lib

source "$BASEDIR/script/common.sh"

SCRIPT_PATH=$(readlink -f $(dirname $0))/
APP=$SCRIPT_PATH/test_multi_thread
config=$@

TEST_TOTAL=26
TEST_FAIL=0

    export LD_PRELOAD=$CXLMALLOCDIR/libcxlmalloc.so

    export CXLMALLOC_CONF=use_exmem:true,use_auto_arena_scaling:true,priority:exmem,exmem_zone_size:2G,normal_zone_size:2G,maxmemory_policy:interleave
    $APP $config
    [ $? != 0 ] && ((TEST_FAIL+=1))
    export CXLMALLOC_CONF=use_exmem:true,use_auto_arena_scaling:true,priority:exmem,exmem_zone_size:2G,normal_zone_size:2G,maxmemory_policy:oom
    $APP $config
    [ $? == 0 ] && ((TEST_FAIL+=1))
    export CXLMALLOC_CONF=use_exmem:true,use_auto_arena_scaling:true,priority:exmem,exmem_zone_size:2G,normal_zone_size:2G,maxmemory_policy:remain
    $APP $config
    [ $? != 0 ] && ((TEST_FAIL+=1))
    export CXLMALLOC_CONF=use_exmem:true,use_auto_arena_scaling:true,priority:normal,exmem_zone_size:2G,normal_zone_size:2G,maxmemory_policy:interleave
    $APP $config
    [ $? != 0 ] && ((TEST_FAIL+=1))
    export CXLMALLOC_CONF=use_exmem:true,use_auto_arena_scaling:true,priority:normal,exmem_zone_size:2G,normal_zone_size:2G,maxmemory_policy:oom
    $APP $config
    [ $? == 0 ] && ((TEST_FAIL+=1))
    export CXLMALLOC_CONF=use_exmem:true,use_auto_arena_scaling:true,priority:normal,exmem_zone_size:2G,normal_zone_size:2G,maxmemory_policy:remain
    $APP $config
    [ $? != 0 ] && ((TEST_FAIL+=1))
    export CXLMALLOC_CONF=use_exmem:true,use_auto_arena_scaling:false,priority:exmem,exmem_zone_size:2G,normal_zone_size:2G,maxmemory_policy:interleave
    $APP $config
    [ $? != 0 ] && ((TEST_FAIL+=1))
    export CXLMALLOC_CONF=use_exmem:true,use_auto_arena_scaling:false,priority:exmem,exmem_zone_size:2G,normal_zone_size:2G,maxmemory_policy:oom
    $APP $config
    [ $? == 0 ] && ((TEST_FAIL+=1))
    export CXLMALLOC_CONF=use_exmem:true,use_auto_arena_scaling:false,priority:exmem,exmem_zone_size:2G,normal_zone_size:2G,maxmemory_policy:remain
    $APP $config
    [ $? != 0 ] && ((TEST_FAIL+=1))
    export CXLMALLOC_CONF=use_exmem:true,use_auto_arena_scaling:false,priority:normal,exmem_zone_size:2G,normal_zone_size:2G,maxmemory_policy:interleave
    $APP $config
    [ $? != 0 ] && ((TEST_FAIL+=1))
    export CXLMALLOC_CONF=use_exmem:true,use_auto_arena_scaling:false,priority:normal,exmem_zone_size:2G,normal_zone_size:2G,maxmemory_policy:oom
    $APP $config
    [ $? == 0 ] && ((TEST_FAIL+=1))
    export CXLMALLOC_CONF=use_exmem:true,use_auto_arena_scaling:false,priority:normal,exmem_zone_size:2G,normal_zone_size:2G,maxmemory_policy:remain
    $APP $config
    [ $? != 0 ] && ((TEST_FAIL+=1))
    export CXLMALLOC_CONF=use_exmem:true,use_auto_arena_scaling:true,priority:exmem,exmem_zone_size:2G,normal_zone_size:2G,maxmemory_policy:interleave,exmem_partition_range:all
    $APP $config
    [ $? != 0 ] && ((TEST_FAIL+=1))
    export CXLMALLOC_CONF=use_exmem:true,use_auto_arena_scaling:true,priority:exmem,exmem_zone_size:2G,normal_zone_size:2G,maxmemory_policy:oom,exmem_partition_range:0
    $APP $config
    [ $? == 0 ] && ((TEST_FAIL+=1))
    export CXLMALLOC_CONF=use_exmem:true,use_auto_arena_scaling:true,priority:exmem,exmem_zone_size:2G,normal_zone_size:2G,maxmemory_policy:remain,exmem_partition_range:1
    $APP $config
    [ $? != 0 ] && ((TEST_FAIL+=1))
    export CXLMALLOC_CONF=use_exmem:true,use_auto_arena_scaling:true,priority:normal,exmem_zone_size:2G,normal_zone_size:2G,maxmemory_policy:interleave,exmem_partition_range:0,1
    $APP $config
    [ $? != 0 ] && ((TEST_FAIL+=1))
    export CXLMALLOC_CONF=use_exmem:true,use_auto_arena_scaling:true,priority:normal,exmem_zone_size:2G,normal_zone_size:2G,maxmemory_policy:oom,exmem_partition_range:0-2
    $APP $config
    [ $? == 0 ] && ((TEST_FAIL+=1))
    export CXLMALLOC_CONF=use_exmem:true,use_auto_arena_scaling:true,priority:normal,exmem_zone_size:2G,normal_zone_size:2G,maxmemory_policy:remain,exmem_partition_range:0,2-3
    $APP $config
    [ $? != 0 ] && ((TEST_FAIL+=1))
    export CXLMALLOC_CONF=use_exmem:true,use_auto_arena_scaling:false,use_adaptive_interleaving:false
    $APP $config
    [ $? != 0 ] && ((TEST_FAIL+=1))
    export CXLMALLOC_CONF=use_exmem:true,use_auto_arena_scaling:true,use_adaptive_interleaving:false
    $APP $config
    [ $? != 0 ] && ((TEST_FAIL+=1))
    export CXLMALLOC_CONF=use_exmem:true,use_auto_arena_scaling:false,use_adaptive_interleaving:true,adaptive_interleaving_policy:bw_saturation
    $APP $config
    [ $? != 0 ] && ((TEST_FAIL+=1))
    export CXLMALLOC_CONF=use_exmem:true,use_auto_arena_scaling:true,use_adaptive_interleaving:true,adaptive_interleaving_policy:bw_saturation
    $APP $config
    [ $? != 0 ] && ((TEST_FAIL+=1))
    export CXLMALLOC_CONF=use_exmem:true,use_auto_arena_scaling:false,use_adaptive_interleaving:true,adaptive_interleaving_policy:bw_saturation,priority:exmem,exmem_zone_size:2G,normal_zone_size:2G,maxmemory_policy:interleave
    $APP $config
    [ $? != 0 ] && ((TEST_FAIL+=1))
    export CXLMALLOC_CONF=use_exmem:true,use_auto_arena_scaling:true,use_adaptive_interleaving:true,adaptive_interleaving_policy:bw_saturation,priority:exmem,exmem_zone_size:2G,normal_zone_size:2G,maxmemory_policy:interleave
    $APP $config
    [ $? != 0 ] && ((TEST_FAIL+=1))
    export CXLMALLOC_CONF=use_exmem:true,use_auto_arena_scaling:false,use_adaptive_interleaving:true,adaptive_interleaving_policy:bw_saturation,priority:exmem,exmem_zone_size:2G,normal_zone_size:2G,maxmemory_policy:interleave
    $APP $config
    [ $? != 0 ] && ((TEST_FAIL+=1))
    export CXLMALLOC_CONF=use_exmem:true,use_auto_arena_scaling:true,use_adaptive_interleaving:true,adaptive_interleaving_policy:bw_saturation,priority:exmem,exmem_zone_size:2G,normal_zone_size:2G,maxmemory_policy:interleave
    $APP $config
    [ $? != 0 ] && ((TEST_FAIL+=1))

echo
if [ $TEST_FAIL != 0 ]; then
    echo "FAIL"
    echo "$TEST_FAIL tests failed among $TEST_TOTAL tests."
    exit 1
fi

echo "PASS"
exit 0

