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

if [ ! -f "${NUMACTL}" ]; then
    log_error "numactl does not exist. Run 'build_lib.sh numactl' in /path/to/SMDK/lib/"
    exit 2
fi

TEST_APP_DIR=$BASEDIR/src/test/numactl/
TEST_APP=$TEST_APP_DIR/simple_malloc

function run_numactl(){
	cd $NUMACTL_DIR
	$NUMACTL --zone $PRIORITY --$MEMPOLICY $NODES --cpunodebind $CPUNODES $TEST_APP
	ret=$?

	if [ $ret != 0 ]; then
		echo "Warning: Retry test with default cpunodebind option"
		$NUMACTL --zone $PRIORITY --$MEMPOLICY $NODES $TEST_APP
        ret=$?
	fi
}

function usage(){
    echo "Usage: $0 [-e | -n] [-i nodes| -p node]"
    exit 2
}

[ $# -eq 0 ] && usage

MEM_SET=0
POL_SET=0

while getopts ":eni:p:" opt; do
	case "$opt" in
        e)
            if [ $MEM_SET == 0 ]; then
                PRIORITY='e'
                MEM_SET=1
            fi
            ;;
        n)
            if [ $MEM_SET == 0 ]; then
                PRIORITY='n'
                MEM_SET=1
            fi
            ;;
        i)
            if [ $POL_SET == 0 ]; then
                MEMPOLICY="interleave"
                NODES=$OPTARG
                POL_SET=1
            fi
            ;;
        p)
            if [ $POL_SET == 0 ]; then
                MEMPOLICY="preferred"
                NODES=$OPTARG
                CPUNODES=$OPTARG
                POL_SET=1
            fi
            ;;
        *)
            usage
            ;;
    esac
done

if [ $POL_SET == 0 ]; then
    usage
fi

run_numactl

echo
if [ $ret == 0 ]; then
	echo "PASS"
else
	echo "FAIL"
	exit 1
fi

exit 0

