#!/usr/bin/env bash

readonly BASEDIR=$(readlink -f $(dirname $0))/../../../

SCRIPT_PATH=$(readlink -f $(dirname $0))/
APP=$SCRIPT_PATH/test_syscall

function run_app(){
	unset LD_PRELOAD
	CXLMALLOC_DIR=$BASEDIR/lib/smdk_allocator/lib/libcxlmalloc.so
	export LD_PRELOAD=$CXLMALLOC_DIR
	CXLMALLOC_CONF=use_exmem:true,exmem_size:4096,normal_size:4096,maxmemory_policy:interleave
        if [ "$PRIORITY" == 'exmem' ]; then
                CXLMALLOC_CONF+=,priority:exmem
        elif [ "$PRIORITY" == 'normal' ]; then
                CXLMALLOC_CONF+=,priority:normal
        fi
	echo $CXLMALLOC_CONF
	export CXLMALLOC_CONF
	$APP
}

function usage(){
    echo "Usage: $0 [-e | -n]"
    exit 2
}

MEM_SET=0

while getopts "en" opt; do
    case "$opt" in
        e)
            if [ $MEM_SET == 0 ]; then
                PRIORITY='exmem'
                MEM_SET=1
            fi
            ;;
        n)
            if [ $MEM_SET == 0 ]; then
                PRIORITY='normal'
                MEM_SET=1
            fi
            ;;
        :)
            usage
            ;;
    esac
done

if [ $MEM_SET == 0]; then
    usage
fi

run_app
ret=$?

echo
if [ $ret == 0 ]; then
	echo "PASS"
else
	echo "FAIL"
	exit 1
fi

exit 0

