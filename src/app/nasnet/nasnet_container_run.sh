#!/usr/bin/env bash

PRIORITY=normal

DATASET_DIR=/tmp/imagenet
EVAL_DIR=/tmp/tfmodel/eval
CHECKPOINT_DIR=/tmp/checkpoints/model.ckpt

IMAGE_TAG=nasnet:smdk

docker run --cap-add=SYS_PTRACE --security-opt seccomp=unconfined -it -e DATASET_DIR=$DATASET_DIR -e EVAL_DIR=$EVAL_DIR -e CHECKPOINT_DIR=$CHECKPOINT_DIR $IMAGE_TAG bash
