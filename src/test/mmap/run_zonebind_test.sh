#!/usr/bin/env bash
readonly BASEDIR=$(readlink -f $(dirname $0))/../../../
source "$BASEDIR/script/common.sh"

# binary path
NUMACTL=$BASEDIR/lib/numactl-2.0.16/numactl
MMAP=$BASEDIR/src/test/mmap/test_mmap_cxl

if [ ! -f "${NUMACTL}" ]; then
    log_error "numactl does not exist. Run 'build_lib.sh numactl' in /path/to/SMDK/lib/"
    exit 2
fi

# test_mmap_cxl parameters
LOOP=500
USEC=1000

# numactl bind node id
DDR_NID=-1
CXL_NID=-1

if [ "$#" -ne 2 ]; then
	echo -e "Usage: $0 <DDR nid> <CXL nid>\n"
	echo -e "If there is no corresponding node, enter -1 as nid. e.g. $0 0 1\n"
	exit 2
fi

DDR_NID=$1
CXL_NID=$2

function print_and_clear_buddyinfo() {
	nid=$1
	log_normal "Before:"
	if [ $nid == -1 ]; then
		cat buddyinfo_before.out
	else
		cat buddyinfo_before.out | grep "Node $nid"
	fi
	rm -rf buddyinfo_before.out

	log_normal "After:"
	if [ $nid == -1 ]; then
		cat buddyinfo_after.out
	else
		cat buddyinfo_after.out | grep "Node $nid"
	fi
	rm -rf buddyinfo_after.out

	echo -e "\n\n"
}

# TC ID, nid to mbind, mmap target memtype option, expected result
param_count=4
tclist=( \
	TC0		-1			e			SUCCESS \
	TC1		-1			n			SUCCESS \
	TC2		$DDR_NID	" "			SUCCESS \
	TC3		$CXL_NID	" "			SUCCESS \
	TC4		$DDR_NID	e			FAILURE \
	TC5		$DDR_NID	n			SUCCESS \
	TC6		$CXL_NID	e			SUCCESS \
	TC7		$CXL_NID	n			FAILURE \
)
tc_cnt=$((${#tclist[@]} / $param_count))

fail_tc_count=$((${#fail_tclist[@]} / $param_count))

function run_testcase() {
	nid=$1
	mmap_option=$2

	if [ $nid != -1 ]; then
		echo "1) target node: $nid"
		echo "2) numactl: -m $nid"
		echo "3) mmap: $mmap_option"
	else
		echo "1) target node: n/a"
		echo "2) numactl: n/a"
		echo "3) mmap: $mmap_option"
	fi

	if [ $nid == -1 ]; then
		$MMAP $mmap_option loop $LOOP usleep $USEC buddyinfo \
			> /dev/null 2>&1
	else
		$NUMACTL -m $nid \
			$MMAP $mmap_option loop $LOOP usleep $USEC buddyinfo \
			> /dev/null 2>&1
	fi
	ret=$?
	if [ $ret != 0 ]; then
		exit 1
	fi
}

for ((i=0; i<${tc_cnt}; i++)); do
	index=$(($param_count * ($i)))
	tcname="${tclist[$index]}"
	nid="${tclist[$(($index + 1))]}"
	mmap_target_memtype="${tclist[$(($index + 2))]}"
	expected_result="${tclist[$(($index + 3))]}"

	if [ $expected_result = "FAILURE" ]; then continue; fi

	clear
	log_normal "RUN $tcname"

	run_testcase "$nid" "$mmap_target_memtype"
	$tc
	log_normal "Expected Result:"
	if [ $nid != -1 ]; then
		echo "target node: $nid"
	fi
	if [ "$mmap_target_memtype" == "e" ]; then
		echo "target memory type: CXL"
	elif [ "$mmap_target_memtype" == "n" ]; then
		echo "target memory type: DDR"
	fi
	print_and_clear_buddyinfo $nid

	log_normal "Press any key to continue"
	echo ""
	while [ true ] ; do
		read -t 3 -n 1
		if [ $? = 0 ] ; then
			break;
		fi
	done
done

exit 0
