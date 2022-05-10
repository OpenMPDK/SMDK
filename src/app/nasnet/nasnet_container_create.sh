#!/usr/bin/env bash

readonly BASEDIR=$(readlink -f $(dirname $0))/../../..

IMAGE_TAG=nasnet:smdk

DOCKERFILE_DIR=$BASEDIR/src/app/nasnet
CXLMALLOC_DIR=$BASEDIR/lib/smdk_allocator/lib/libcxlmalloc.so

cp $CXLMALLOC_DIR $DOCKERFILE_DIR

IMAGENET_DATA=ImageNet.tar.gz
tar -zxvf $IMAGENET_DATA

docker build -t $IMAGE_TAG .

