# prerequisite:
# 1. SMDK Kernel is running
# 2. $BASEDIR/lib/build_lib.sh numactl
#!/usr/bin/env bash
readonly BASEDIR=$(readlink -f $(dirname $0))/../../../

PRIORITY=n
MEMPOLICY=interleave
NODES=all
CPUNODES=all

NUMACTL_DIR=$BASEDIR/lib/numactl-2.0.14/
NUMACTL=$NUMACTL_DIR/numactl

TEST_APP_DIR=$BASEDIR/src/test/numactl/
TEST_APP=$TEST_APP_DIR/simple_malloc

function run_numactl(){
	cd $NUMACTL_DIR
	$NUMACTL --zone $PRIORITY --$MEMPOLICY $NODES --cpunodebind $CPUNODES $TEST_APP
	ret=$?
	if [ $ret != 0 ]; then
		echo "Warning: Retry test with default cpunodebind option"
		$NUMACTL --zone $PRIORITY --$MEMPOLICY $NODES $TEST_APP
	fi
}

usage() { echo "Usage: $0 [-e | -n] [-i nodes| -m nodes]"; exit 0; }
[ $# -eq 0 ] && usage
while getopts ":eni:m:" opt; do
	case "$opt" in
		e)
			PRIORITY='e' # ExMem zone
			;;
		n)
			PRIORITY='n' # Normal zone
			;;
		i)
			MEMPOLICY="interleave"
			NODES=$OPTARG
			;;
		m)
			MEMPOLICY="membind"
			NODES=$OPTARG
			CPUNODES=$OPTARG
			;;
		*)
			usage
			;;
	esac
done

run_numactl
