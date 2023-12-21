#!/usr/bin/env bash
readonly BASEDIR=$(readlink -f $(dirname $0))/../../../

SCRIPT_PATH=$(readlink -f $(dirname $0))/
APP=$SCRIPT_PATH/test_pnm_imdb
PNM_CTL=$BASEDIR/lib/PNMLibrary-pnm-v3.0.0/PNMLibrary/build/tools/pnm_ctl

function install_imdb_driver(){
    sudo modprobe -v imdb_resource
    $PNM_CTL setup-shm --imdb
}

function uninstall_imdb_driver(){
    $PNM_CTL destroy-shm --imdb
    sudo rmmod imdb_resource
}

function run_app(){
## dynamic link
    export LD_LIBRARY_PATH=$BASEDIR/lib/smdk_allocator/lib
## turn off debug log from PNMLibrary
    export SPDLOG_LEVEL=main=off

    $APP
}

install_imdb_driver

run_app
ret=$?

uninstall_imdb_driver

echo
if [ $ret == 0 ]; then
    echo "PASS"
else
    echo "FAIL"
    exit 1
fi

exit 0
