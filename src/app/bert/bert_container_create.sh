#!/usr/bin/env bash
 
readonly BASEDIR=$(readlink -f $(dirname $0))/../../..

IMAGE_TAG=bert:smdk

DOCKERFILE_DIR=$BASEDIR/src/app/bert
PYTHON_SRC=$BASEDIR/lib/Python-3.7.12
CXLMALLOC_DIR=$BASEDIR/lib/smdk_allocator/lib/libcxlmalloc.so
 
cp -r $PYTHON_SRC $DOCKERFILE_DIR
cp $CXLMALLOC_DIR $DOCKERFILE_DIR

BERT_MODEL=bert_model.tar.gz
GLUE_DATA=glue_data.tar.gz

tar -zxvf $BERT_MODEL
tar -zxvf $GLUE_DATA

docker build -t $IMAGE_TAG .
