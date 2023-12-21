#!/usr/bin/env bash
readonly BASEDIR=$(readlink -f $(dirname $0))/../../../

PRIORITY=normal

function run_app(){
	unset LD_PRELOAD
	export LD_PRELOAD=/lib/libcxlmalloc.so
    CXLMALLOC_CONF=use_exmem:true,exmem_size:16384,normal_size:16384,maxmemory_policy:remain
    if [ "$PRIORITY" == 'exmem' ]; then
        CXLMALLOC_CONF+=,priority:exmem
		export CXLMALLOC_CONF
		echo $CXLMALLOC_CONF
		python3 -C -O /usr/src/nasnet/eval_image_classifier.py \
			--checkpoint_path=$CHECKPOINT_DIR \
			--eval_dir=$EVAL_DIR \
			--dataset_dir=$DATASET_DIR \
			--dataset_name=imagenet \
			--dataset_split_name=validation \
			--model_name=nasnet_large \
			--eval_image_size=331
    elif [ "$PRIORITY" == 'normal' ]; then
        CXLMALLOC_CONF+=,priority:normal
		export CXLMALLOC_CONF
		echo $CXLMALLOC_CONF
		python3 -O /usr/src/nasnet/eval_image_classifier.py \
			--checkpoint_path=$CHECKPOINT_DIR \
			--eval_dir=$EVAL_DIR \
			--dataset_dir=$DATASET_DIR \
			--dataset_name=imagenet \
			--dataset_split_name=validation \
			--model_name=nasnet_large \
			--eval_image_size=331
    fi
}

while getopts ":ena" opt; do
	case "$opt" in
		e)
			PRIORITY='exmem'
			;;
		n)
			PRIORITY='normal'
			;;
		a)
			run_app
			;;
		:)
			echo "Usage: $0 -enlp"
			echo "Usage: $0 -e -a | -n -a"
			echo "Usage: $0 -e -p | -n -p"
	esac

done
