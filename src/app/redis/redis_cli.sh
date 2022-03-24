#!/usr/bin/env bash
# prerequisite: redis installation
readonly BASEDIR=$(readlink -f $(dirname $0))/../../../
REDIS=redis-6.2.1
REDISDIR=$BASEDIR/lib/$REDIS/src/
cd $REDISDIR

unset LD_PRELOAD
CXLMALLOC=$BASEDIR/lib/smdk_allocator/lib/libcxlmalloc.so
export LD_PRELOAD=$CXLMALLOC


if [ "$1" = "" ]; then
	./redis-cli
else
	./redis-cli "$1" "$2" "$3"
fi

# info
# config get * 
# set timeout 900

