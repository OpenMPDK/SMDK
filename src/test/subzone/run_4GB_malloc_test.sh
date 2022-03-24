# prerequisite:
# 1. SMDK Kernel is running
# 2. $BASEDIR/lib/build_lib.sh numactl
#!/usr/bin/env bash
readonly BASEDIR=$(readlink -f $(dirname $0))/../../../
source "$BASEDIR/script/common.sh"

# numactl
NUMACTL_DIR=$BASEDIR/lib/numactl-2.0.14/
NUMACTL=$NUMACTL_DIR/numactl

# cxl policy
CXL_SYSFS_PATH="/sys/kernel/cxl/"
POLICY_PATH=$CXL_SYSFS_PATH/cxl_group_policy
MODE_PATH=$CXL_SYSFS_PATH/cxl_mem_mode

# tc
TEST_APP_DIR=$BASEDIR/src/test/subzone/
TEST_APP=$TEST_APP_DIR/test_multithread_malloc
SIZE=4096		#4k
ITER=1048576	# 1024 * 1024
NTHREAD=1

run_malloc() {
	$NUMACTL --zone e --interleave all $TEST_APP --size $SIZE --iter $ITER \
		--thread $NTHREAD
}

set_pol() {
	echo $POL > $POLICY_PATH
}

print_desc_test() {
	echo -n "cxl_group_policy: $POL, "
	echo -n "size: `numfmt --to iec --format "%f" $SIZE` bytes, "
	echo -n "iteration: `numfmt --to iec --format "%f" $ITER` times, "
}

get_time() {
   	start_time=$(date +%s%N)
}

print_elapsed_time() {
	elapsed=$((($(date +%s%N) - $start_time)/1000000))
	echo "elasepd time: $elapsed milliseconds"
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
	log_error "You must be root!"
	exit
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
