#!/bin/bash
# note : prerequisite - cgdb, gdb also works though

readonly BASEDIR=$(readlink -f $(dirname $0))/../../

SMDK_KERNEL_PATH=${BASEDIR}/lib/linux-6.4-smdk

cgdb -q $SMDK_KERNEL_PATH/vmlinux -ex 'target remote localhost:12345'
#gdb -q $SMDK_KERNEL_PATH/vmlinux -ex 'target remote /dev/pts/4'
