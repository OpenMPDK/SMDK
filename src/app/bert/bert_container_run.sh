#!/usr/bin/env bash
 
PRIORITY=normal
 
BERT_BASE_DIR=/path/to/bert/wwm_cased_L-24_H-1024_A-16
GLUE_DIR=/path/to/glue
TASK_NAME=CoLA
 
IMAGE_TAG=bert:smdk
 
docker run --cap-add=SYS_PTRACE --security-opt seccomp=unconfined -it -e BERT_BASE_DIR=$BERT_BASE_DIR -e GLUE_DIR=$GLUE_DIR -e TASK_NAME=$TASK_NAME $IMAGE_TAG bash
