#!/usr/bin/env bash
# prerequisite:
# 1. SMDK Kernel is running
# 2. $BASEDIR/lib/build_lib.sh numactl

readonly BASEDIR=$(readlink -f $(dirname $0))/../../../
source "$BASEDIR/script/common.sh"

# numactl
NUMACTL_DIR=$BASEDIR/lib/numactl-2.0.14/
NUMACTL=$NUMACTL_DIR/numactl

# cxl-cli
CXLCLI=$BASEDIR/lib/cxl_cli/build/cxl/cxl

# tc
TEST_APP_DIR=$BASEDIR/src/test/subzone/
TEST_APP=$TEST_APP_DIR/test_multithread_malloc
SIZE=4096		#4k
ITER=1048576	# 1024 * 1024
NTHREAD=1

run_malloc() {
	$NUMACTL --zone e --interleave all $TEST_APP --size $SIZE --iter $ITER \
		--thread $NTHREAD

	ret=$?
	if [ $ret != 0 ]; then
		echo "FAIL"
		exit 1
	fi
}

set_pol() {
	if [ "$POL" = "zone" ]; then
		$CXLCLI group-zone
	elif [ "$POL" = "node" ]; then
		$CXLCLI group-node
	elif [ "$POL" = "noop" ]; then
		$CXLCLI group-noop
	else
		log_error "Bad policy: $POL"
		exit 2
	fi

	ret=$?
	if [ $ret != 0 ]; then
		log_error "Faild to set policy, check out cxl_cli functionality."
		exit 1
	fi
}

print_desc_test() {
	echo -n "cxl group-$POL, "
	echo -n "size: `numfmt --to iec --format "%f" $SIZE` bytes, "
	echo -n "iteration: `numfmt --to iec --format "%f" $ITER` times, "
}

get_time() {
   	start_time=$(date +%s%N)
}

print_elapsed_time() {
	elapsed=$((($(date +%s%N) - $start_time)/1000000))
	echo "elapsed time: $elapsed milliseconds"
}

run_test() {
	POL="zone"
	print_desc_test; set_pol; get_time; run_malloc; print_elapsed_time

	POL="node"
	print_desc_test; set_pol; get_time; run_malloc; print_elapsed_time

	POL="noop"
	print_desc_test; set_pol; get_time; run_malloc; print_elapsed_time
}

single_thread_testcase=( \
	test_malloc_1K_bytes_4M_times \
	test_malloc_4K_bytes_1M_times \
	test_malloc_128K_bytes_32K_times \
	test_malloc_4M_bytes_1K_times \
)

test_malloc_1K_bytes_4M_times(){
   	SIZE=1024; ITER=4194304; run_test;
}
test_malloc_4K_bytes_1M_times(){
   	SIZE=4096; ITER=1048576; run_test;
}
test_malloc_128K_bytes_32K_times(){
   	SIZE=131072; ITER=32768; run_test;
}
test_malloc_4M_bytes_1K_times(){
   	SIZE=4194304; ITER=1024; run_test;
}

multi_thread_testcase=( \
	test_malloc_4GB_10_threads_1K_unit \
	test_malloc_4GB_10_threads_4K_unit \
	test_malloc_4GB_10_threads_128K_unit \
	test_malloc_4GB_10_threads_4M_unit \
)

test_malloc_4GB_10_threads_1K_unit(){
	SIZE=1024; ITER=419430; NTHREAD=10; run_test;
}
test_malloc_4GB_10_threads_4K_unit(){
	SIZE=4096; ITER=104857; NTHREAD=10; run_test;
}
test_malloc_4GB_10_threads_128K_unit(){
	SIZE=131072; ITER=3276; NTHREAD=10; run_test;
}
test_malloc_4GB_10_threads_4M_unit(){
	SIZE=4194304; ITER=102; NTHREAD=10; run_test;
}

if [ `whoami` != 'root' ]; then
	log_error "This test requires root privileges"
	exit 2
fi

if [ ! -f "${NUMACTL}" ]; then
	log_error "numactl does not exist. Run 'build_lib.sh numactl' in /path/to/SMDK/lib/"
	exit 2
fi

if [ ! -f "${CXLCLI}" ]; then
	log_error "cxl does not exist. Run 'build_lib.sh cxl_cli' in /path/to/SMDK/lib/"
	exit 2
fi

log_normal "Single Thread Testcases"
for i in "${single_thread_testcase[@]}"
do
	log_normal "TC $i starts"
	$i
	log_normal "TC $i done"
done

log_normal "Multi Thread Testcases"
for i in "${multi_thread_testcase[@]}"
do
	log_normal "TC $i starts"
	$i
	log_normal "TC $i done"
done

echo
echo "PASS"
exit 0

