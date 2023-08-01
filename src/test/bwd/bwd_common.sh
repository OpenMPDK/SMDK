#!/bin/bash

readonly BASEDIR=$(readlink -f $(dirname $0))/../../../
source "$BASEDIR/script/common.sh"

### PATH

BWDDIR=$BASEDIR/lib/bwd/
BWD_DD=$BWDDIR/drivers/bwd.ko
BWD_CONFPATH=$BWDDIR/bwd.conf
BWD_DEVPATH=/dev/bwd
BWD=$BWDDIR/bwd
BWDMAP=/run/bwd

readonly TEST_SUCCESS=0
readonly TEST_FAILURE=1
readonly ENV_SET_FAIL=2

### functions
