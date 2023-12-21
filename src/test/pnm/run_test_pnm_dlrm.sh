#!/usr/bin/env bash
readonly BASEDIR=$(readlink -f $(dirname $0))/../../../

SCRIPT_PATH=$(readlink -f $(dirname $0))/
APP=$SCRIPT_PATH/test_pnm_dlrm
PNM_CTL=$BASEDIR/lib/PNMLibrary-pnm-v3.0.0/PNMLibrary/build/tools/pnm_ctl

CFG="row 500000"
if [ ! -z $RUN_ON_QEMU ]; then
    CFG="row 50000"
fi

function install_sls_driver(){
    sudo modprobe -v sls_resource
    $PNM_CTL setup-shm --sls
}

function uninstall_sls_driver(){
    $PNM_CTL destroy-shm --sls
    sudo rmmod sls_resource
}

function run_app(){
## dynamic link
    export LD_LIBRARY_PATH=$BASEDIR/lib/smdk_allocator/lib
## turn off debug log from PNMLibrary
    export SPDLOG_LEVEL=main=off

    $APP $CFG
}

install_sls_driver

run_app
ret=$?

uninstall_sls_driver

echo
if [ $ret == 0 ]; then
    echo "PASS"
else
    echo "FAIL"
    exit 1
fi

exit 0
