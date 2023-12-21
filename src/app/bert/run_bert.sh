#!/usr/bin/env bash
readonly BASEDIR=$(readlink -f $(dirname $0))/../../../

PRIORITY=normal
#PYTHON=python

function run_app(){
	unset LD_PRELOAD
	export LD_PRELOAD=/lib/libcxlmalloc.so
    CXLMALLOC_CONF=use_exmem:true,exmem_size:16384,normal_size:16384,maxmemory_policy:remain
    if [ "$PRIORITY" == 'exmem' ]; then
        CXLMALLOC_CONF+=,priority:exmem
		export CXLMALLOC_CONF
		echo $CXLMALLOC_CONF
		python3 -C -O /usr/src/bert/run_classifier.py \
					  --task_name=$TASK_NAME \
					  --do_predict=true \
					  --data_dir=$GLUE_DIR/$TASK_NAME \
					  --vocab_file=$BERT_BASE_DIR/vocab.txt \
					  --bert_config_file=$BERT_BASE_DIR/bert_config.json \
					  --init_checkpoint=$BERT_BASE_DIR/bert_model.ckpt \
					  --max_seq_length=128 \
					  --predict_batch_size=4 \
					  --output_dir=/tmp/output/$TASK_NAME/
    elif [ "$PRIORITY" == 'normal' ]; then
        CXLMALLOC_CONF+=,priority:normal
		export CXLMALLOC_CONF
		echo $CXLMALLOC_CONF
		python3 -O /usr/src/bert/run_classifier.py \
					  --task_name=$TASK_NAME \
					  --do_predict=true \
					  --data_dir=$GLUE_DIR/$TASK_NAME \
					  --vocab_file=$BERT_BASE_DIR/vocab.txt \
					  --bert_config_file=$BERT_BASE_DIR/bert_config.json \
					  --init_checkpoint=$BERT_BASE_DIR/bert_model.ckpt \
					  --max_seq_length=128 \
					  --predict_batch_size=4 \
					  --output_dir=/tmp/output/$TASK_NAME/
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
