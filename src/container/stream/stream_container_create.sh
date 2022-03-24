#!/bin/sh


readonly BASEDIR=$(readlink -f $(dirname $0))/../../..

STREAM=stream
STREAM_SRC=$BASEDIR/lib/$STREAM
DOCKERFILE_DIR=$BASEDIR/src/container/stream
CXLMALLOC_LIB=$BASEDIR/lib/smdk_allocator/lib/libcxlmalloc.so
BUILD_SRC=$BASEDIR/lib/build_lib.sh
COMMON_DIR=$BASEDIR/script/common.sh

cd $DOCKERFILE_DIR
rm -rf $STREAM
mkdir $STREAM
cp -r $STREAM_SRC $DOCKERFILE_DIR
cp $CXLMALLOC_LIB $DOCKERFILE_DIR
cp $BUILD_SRC $DOCKERFILE_DIR
cp $COMMON_DIR $DOCKERFILE_DIR
docker build -t stream_test:smdk .



