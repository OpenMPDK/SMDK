#!/usr/bin/env bash
# prerequisite: redis installation
#set -x
readonly BASEDIR=$(readlink -f $(dirname $0))/../../../
REDIS=redis-6.2.1

PRIORITY=exmem

IMAGE_TAG=redis:smdk

function run_app(){
	rm *rdb *aof 2>/dev/null
	echo 3 > /proc/sys/vm/drop_caches

	CXLMALLOC_LIB=/usr/lib/libcxlmalloc.so
	CXLMALLOC_CONF=use_exmem:true,exmem_size:16384,normal_size:16384,maxmemory_policy:remain
	if [ "$PRIORITY" == 'exmem' ]; then
		CXLMALLOC_CONF+=,priority:exmem
	elif [ "$PRIORITY" == 'normal' ]; then
		CXLMALLOC_CONF+=,priority:normal
	fi
	
	echo $CXLMALLOC_CONF
	docker run -it -p 6379:6379 -e LD_PRELOAD=$CXLMALLOC_LIB -e CXLMALLOC_CONF=$CXLMALLOC_CONF $IMAGE_TAG --loglevel verbose
}

while getopts ":en" opt; do
	case "$opt" in
		e)
			PRIORITY="exmem"
			run_app
			;;
		n)
			PRIORITY="normal"
			run_app
			;;
		:)
			echo "Usage: $0 -e[exmem] or $0 -n[normal]"
			exit 1
			;;
	esac
done

# Run app
