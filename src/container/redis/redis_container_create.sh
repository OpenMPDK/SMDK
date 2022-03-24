#!/usr/bin/env bash

readonly BASEDIR=$(readlink -f $(dirname $0))/../../..

IMAGE_TAG=redis:smdk

REDIS=redis-6.2.1
REDIS_SRC=$BASEDIR/lib/$REDIS
DOCKERFILE_DIR=$BASEDIR/src/container/redis
CXLMALLOC_LIB=$BASEDIR/lib/smdk_allocator/lib/libcxlmalloc.so
CONF_NAME=redis.summary.conf
REDIS_CONF_SRC=$BASEDIR/src/app/redis/$CONF_NAME

cd $DOCKERFILE_DIR
rm -rf $CONF_NAME $REDIS
cp -r $REDIS_SRC $DOCKERFILE_DIR
cp $REDIS_CONF_SRC $DOCKERFILE_DIR
cp $CXLMALLOC_LIB $DOCKERFILE_DIR

docker build -t $IMAGE_TAG .
