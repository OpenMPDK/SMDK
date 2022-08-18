#!/usr/bin/env bash
readonly BASEDIR=$(readlink -f $(dirname $0))/../../../
source "$BASEDIR/script/common.sh"

# binary path
NUMACTL=$BASEDIR/lib/numactl-2.0.14/numactl
MMAP=$BASEDIR/src/test/mmap/test_mmap_cxl

# test_mmap_cxl parameters
LOOP=500
USEC=1000

# numactl bind node id
DDR_ONLY_NID=-1
CXL_ONLY_NID=-1
DDR_CXL_NID=-1

if [ "$#" -ne 3 ]; then
	echo -e "Usage: $0 <DDR only nid> <CXL only nid> <DDR + CXL nid>\n"
	echo -e "If there is no corresponding node, enter -1 as nid. e.g. $0 0 1 -1\n"
	exit
fi

DDR_ONLY_NID=$1
CXL_ONLY_NID=$2
DDR_CXL_NID=$3

function print_and_clear_buddyinfo() {
	nid=$1
	zone=$2
	log_normal "Before:"
	cat buddyinfo_before.out | grep "Node $nid" | grep $zone
	rm -rf buddyinfo_before.out

	log_normal "After:"
	cat buddyinfo_after.out | grep "Node $nid" | grep $zone
	rm -rf buddyinfo_after.out

	echo -e "\n\n"
}

# TC ID, nid, numactl zone bind option, mmap target zone option, expected result
param_count=5
tclist=( \
	TC0		$DDR_ONLY_NID	" "		e		FAILURE \
	TC1		$DDR_ONLY_NID	" "		n		SUCCESS \
	TC2		$CXL_ONLY_NID	" "		e		SUCCESS \
	TC3		$CXL_ONLY_NID	" "		n		FAILURE \
	TC4		$DDR_CXL_NID	" "		e		SUCCESS \
	TC5		$DDR_CXL_NID	" "		n		SUCCESS \
	TC6		$DDR_ONLY_NID	"-z e"	" "		FAILURE \
	TC7		$DDR_ONLY_NID	"-z n"	" "		SUCCESS \
	TC8		$CXL_ONLY_NID	"-z e"	" "		SUCCESS \
	TC9		$CXL_ONLY_NID	"-z n"	" "		FAILURE	\
	TC10	$DDR_CXL_NID	"-z e"	" "		SUCCESS \
	TC11	$DDR_CXL_NID	"-z n"	" "		SUCCESS \
	TC12	$DDR_ONLY_NID	"-z e"	e		FAILURE \
	TC13	$DDR_ONLY_NID	"-z e"	n		FAILURE	\
	TC14	$DDR_ONLY_NID	"-z n"	e		FAILURE \
	TC15	$DDR_ONLY_NID	"-z n"	n		SUCCESS \
	TC16	$CXL_ONLY_NID	"-z e"	e		SUCCESS \
	TC17	$CXL_ONLY_NID	"-z e"	n		FAILURE \
	TC18	$CXL_ONLY_NID	"-z n"	e		FAILURE \
	TC19	$CXL_ONLY_NID	"-z n"	n		FAILURE \
	TC20	$DDR_CXL_NID	"-z e"	e		SUCCESS \
	TC21	$DDR_CXL_NID	"-z e"	n		FAILURE \
	TC22	$DDR_CXL_NID	"-z n"	e		FAILURE \
	TC23	$DDR_CXL_NID	"-z n"	n		SUCCESS
)
tc_cnt=$((${#tclist[@]} / $param_count))

fail_tc_count=$((${#fail_tclist[@]} / $param_count))

function run_testcase() {
	nid=$1
	zonebind_option=$2
	mmap_option=$3

	echo "1) target node: $nid"
	echo "2) numactl: $zonebind_option"
	echo "3) mmap: $mmap_option"

	$NUMACTL $zonebind_option --preferred $nid \
		$MMAP $mmap_option loop $LOOP usleep $USEC buddyinfo \
		> /dev/null 2>&1
}

function get_expected_zone() {
	numactl_zone=$1
	mmap_zone=$2

	if [ -n "$numactl_zone" ] && [ "$numactl_zone" = "-z n" ]; then echo "Normal"
	elif [ -n "$numactl_zone" ] && [ "$numactl_zone" = "-z e" ]; then echo "ExMem"
	elif [ -n "$mmap_zone" ] && [ "$mmap_zone" = "n" ];then echo "Normal"
	elif [ -n "$mmap_zone" ] && [ "$mmap_zone" = "e" ]; then echo "ExMem"
	fi
}

for ((i=0; i<${tc_cnt}; i++)); do
	index=$(($param_count * ($i)))
	tcname="${tclist[$index]}"
	nid="${tclist[$(($index + 1))]}"
	numactl_zonebind_option="${tclist[$(($index + 2))]}"
	mmap_target_zone="${tclist[$(($index + 3))]}"
	expected_result="${tclist[$(($index + 4))]}"
	zone=$(get_expected_zone "$numactl_zonebind_option" "$mmap_target_zone")

	if [ $expected_result = "FAILURE" ]; then continue; fi

	clear
	log_normal "RUN $tcname"

	if [ $nid != -1 ]; then
		run_testcase "$nid" "$numactl_zonebind_option" "$mmap_target_zone"
		$tc
		log_normal "Expected Result: Node $nid, zone $zone"
		print_and_clear_buddyinfo $nid $zone
	else
		log_error "Target node id is $nid. pass $tcname"
	fi

	log_normal "Press any key to continue"
	echo ""
	while [ true ] ; do
		read -t 3 -n 1
		if [ $? = 0 ] ; then
			break;
		fi
	done
done

