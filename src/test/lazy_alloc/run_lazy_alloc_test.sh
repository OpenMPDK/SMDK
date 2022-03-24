#!/usr/bin/env bash
readonly BASEDIR=$(readlink -f $(dirname $0))/../../../
source "$BASEDIR/script/common.sh"

LAZY_ALLOC_DIR=$BASEDIR/src/test/lazy_alloc/
LAZY_ALLOC=$LAZY_ALLOC_DIR/test_lazy_alloc

while getopts ":t:p:" opt; do
	case "$opt" in
		t)
			TYPE=$OPTARG
			;;
		p)	
			SIZE=$OPTARG
			;;
		\?)
			echo "Usage: run_lazy_alloc_test.sh -t (n or e) -p (32)"
			exit 1
			;;
		:)
			echo "Usage: run_lazy_alloc_test.sh -t (n or e) -p (32)"
			exit 1
			;;
	esac
done

$LAZY_ALLOC -t $TYPE -p $SIZE

exit 0
