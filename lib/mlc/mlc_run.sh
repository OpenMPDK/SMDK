#!/usr/bin/env bash

readonly BASEDIR=$(readlink -f $(dirname $0))/../../

CXLMALLOC=$BASEDIR/lib/smdk_allocator/lib/libcxlmalloc.so
MLC=$BASEDIR/lib/mlc/Linux/mlc
CLI=$BASEDIR/lib/cxl_cli/build/cxl/cxl
GROUP=group-noop # group-zone, group-node

CONF_PATH=/proc/sys/vm/numa_zonelist_order

if [ ! -e $CXL_CLI ]; then
	echo "cxl-cli does not exist. Run './build_lib.sh cxl_cli' at /path/to/SMDK/lib/"
	exit
fi

if [ `whoami` != 'root' ]; then
	echo "This script requires root privileges"
	exit
fi

$CLI $GROUP
echo exmem > $CONF_PATH		# zone traversal on (exmem or normal)
$MLC --latency_matrix
$MLC --bandwidth_matrix
echo none > $CONF_PATH		# zone traversal off
