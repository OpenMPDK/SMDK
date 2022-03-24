#!/usr/bin/env bash

readonly BASEDIR=$(readlink -f $(dirname $0))/../../..

MEMCACHED=memcached-1.6.9
MEMCACHED_SRC=$BASEDIR/lib/$MEMCACHED
DOCKERFILE_DIR=$BASEDIR/src/container/memcached
CXLMALLOC_LIB=$BASEDIR/lib/smdk_allocator/lib/libcxlmalloc.so

cd $DOCKERFILE_DIR
rm -rf $MEMCACHED
mkdir $MEMCACHED
cp -r $MEMCACHED_SRC $DOCKERFILE_DIR
cp $CXLMALLOC_LIB $DOCKERFILE_DIR
docker build -t memcached:smdk .
