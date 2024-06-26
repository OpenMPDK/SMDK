#!/bin/bash

readonly BASEDIR=$(readlink -f $(dirname $0))/../../../
source "$BASEDIR/script/common.sh"

### PATH

TIERDDIR=$BASEDIR/lib/tierd/
TIERD_DD=$BASEDIR/lib/linux-6.9-smdk/drivers/dax/kmem.ko
TIERD_CONFPATH=$TIERDDIR/tierd.conf
TIERD_DEVPATH=/dev/tierd
TIERD=$TIERDDIR/tierd
TIERDMAP=/run/tierd
DAXCTL=$BASEDIR/lib/cxl_cli/build/daxctl/daxctl

readonly TEST_SUCCESS=0
readonly TEST_FAILURE=1
readonly ENV_SET_FAIL=2

### functions
