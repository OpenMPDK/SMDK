#!/usr/bin/env bash
###################################################################
# TC : test allocator configurations for CXL work well
# prerequisite : install cxlmalloc
###################################################################
readonly BASEDIR=$(readlink -f $(dirname $0))/../../../../
source "$BASEDIR/script/common.sh"

SCRIPT_PATH=$(readlink -f $(dirname $0))/

CXLMALLOCDIR=$BASEDIR/lib/smdk_allocator/lib
export LD_PRELOAD=$CXLMALLOCDIR/libcxlmalloc.so

testcase=( t1 t2 t3 t4 t5 t6 t7 t8 t9 t10 t11 t12 t13 t14 t15 t16 t17 t18 t19 t20 t21 t22 t23)

function t1(){
    CXLMALLOC_CONF=use_exmem:true
    run_test $CXLMALLOC_CONF
}

function t2(){
    CXLMALLOC_CONF=use_exmem:false
    run_test $CXLMALLOC_CONF
}

function t3(){
    CXLMALLOC_CONF=use_exmem:true,exmem_zone_size:131072
    run_test $CXLMALLOC_CONF
}

function t4(){
    CXLMALLOC_CONF=use_exmem:true,normal_zone_size:2048
    run_test $CXLMALLOC_CONF
}

function t5(){
    CXLMALLOC_CONF=use_exmem:false,exmem_zone_size:512,normal_zone_size:4096
    run_test $CXLMALLOC_CONF
}

function t6(){
    CXLMALLOC_CONF=priority:exmem
    run_test $CXLMALLOC_CONF
}

function t7(){
    CXLMALLOC_CONF=use_exmem:true,exmem_zone_size:512,normal_zone_size:4096,priority:exmem
    run_test $CXLMALLOC_CONF
}

function t8(){
    # zone size unit: m/M
    CXLMALLOC_CONF=use_exmem:true,exmem_zone_size:512m,normal_zone_size:4096M,priority:exmem
    run_test $CXLMALLOC_CONF
}

function t9(){
    # zone size unit: g/G
    CXLMALLOC_CONF=use_exmem:true,exmem_zone_size:2g,normal_zone_size:4G,priority:exmem
    run_test $CXLMALLOC_CONF
}

function t10(){
    # zone size: -1(unlimited)
    CXLMALLOC_CONF=use_exmem:true,exmem_zone_size:-1,normal_zone_size:-1,priority:exmem
    run_test $CXLMALLOC_CONF
}

function t11(){
    # the number of cxl/normal arena = # cpu * auto arena scale
    CXLMALLOC_CONF=use_exmem:true,use_auto_arena_scaling:true
    run_test $CXLMALLOC_CONF
}

function t12(){
    # maxmemory_policy = oom
    CXLMALLOC_CONF=use_exmem:true,maxmemory_policy:oom
    run_test $CXLMALLOC_CONF
}

function t13(){
    # maxmemory_policy = interleave
    CXLMALLOC_CONF=use_exmem:true,maxmemory_policy:interleave
    run_test $CXLMALLOC_CONF
}

function t14(){
    # maxmemory_policy = remain
    CXLMALLOC_CONF=use_exmem:true,maxmemory_policy:remain
    run_test $CXLMALLOC_CONF
}

function t15(){
    # exmem_partition_range: all
    CXLMALLOC_CONF=use_exmem:true,priority:exmem,exmem_partition_range:all
    run_test $CXLMALLOC_CONF
}

function t16(){
    # exmem_partition_range: N,N
    CXLMALLOC_CONF=use_exmem:true,priority:exmem,exmem_partition_range:0,1,2
    run_test $CXLMALLOC_CONF
}

function t17(){
    # exmem_partition_range: N-N
    CXLMALLOC_CONF=use_exmem:true,priority:exmem,exmem_partition_range:1-3
    run_test $CXLMALLOC_CONF
}

function t18(){
    CXLMALLOC_CONF=use_exmem:true,use_adaptive_interleaving:false
    run_test $CXLMALLOC_CONF
}

function t19(){
    CXLMALLOC_CONF=use_exmem:true,use_adaptive_interleaving:true
    run_test $CXLMALLOC_CONF
}

function t20(){
    CXLMALLOC_CONF=use_exmem:true,use_adaptive_interleaving:true,adaptive_interleaving_policy:bw_saturation
    run_test $CXLMALLOC_CONF
}

function t21(){
    CXLMALLOC_CONF=use_exmem:true,use_adaptive_interleaving:true,adaptive_interleaving_policy:bw_saturation,priority:exmem
    run_test $CXLMALLOC_CONF
}

function t22(){
    CXLMALLOC_CONF=use_exmem:true,use_adaptive_interleaving:true,adaptive_interleaving_policy:bw_saturation,maxmemory_policy:interleave
    run_test $CXLMALLOC_CONF
}

function t23(){
    CXLMALLOC_CONF=use_exmem:true,use_adaptive_interleaving:true,adaptive_interleaving_policy:bw_saturation,exmem_zone_size:2g,normal_zone_size:4G
    run_test $CXLMALLOC_CONF
}

function run_test(){
    unset CXLMALLOC_CONF
    CXLMALLOC_CONF=$1
    echo $CXLMALLOC_CONF 
    echo "--------------------------------"
    export CXLMALLOC_CONF
    ls
}

cd $SCRIPT_PATH

for i in "${testcase[@]}"
do
    log_normal "run test - $i"
    $i
done

exit 0
