#!/usr/bin/env bash
REDIS_IP=localhost
REDIS_PORT=6379
#set -x

unset LD_PRELOAD
CXLMALLOC=$BASEDIR/lib/smdk_allocator/lib/libcxlmalloc.so
export LD_PRELOAD=$CXLMALLOC

telnet $REDIS_IP $REDIS_PORT

# set a b
# get a
# quit
