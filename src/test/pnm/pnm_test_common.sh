#!/bin/bash

readonly BASEDIR=$(readlink -f $(dirname $0))/../../../
source "$BASEDIR/script/common.sh"

readonly TEST_SUCCESS=0
readonly TEST_FAILURE=1
readonly ENV_SET_FAIL=2

#BINARY PATH
DAXCTL="$BASEDIR/lib/cxl_cli/build/daxctl/daxctl"
DEVDAX="/dev/dax0.0"
PNM_CTL="$BASEDIR/lib/PNMLibrary-pnm-v3.0.0/PNMLibrary/build/tools/pnm_ctl"

function check_path_exist() {
        if [ ! -e $1 ]; then
                echo "$1 for PNM resource was not created."
                exit $TEST_FAILURE
        fi
}

function check_path_not_exist() {
        if [ -e $1 ]; then
                echo "$1 is already exist. Please remove $1 for using PNM resource."
                exit $ENV_SET_FAIL
        fi
}

function check_pnmctl_installed() {
        if [ ! -e $PNM_CTL ]; then
                echo "pnm_ctl doesn't exist. Run 'build_lib.sh libpnm' in /path/to/SMDK/lib/."
                exit $ENV_SET_FAIL
        fi
}

function remove_module() {
	if [ -n "$(lsmod | grep $1)" ]; then
		sudo rmmod $1	
	fi
}

function convert_to_devdax() {
	sleep 1
        if [ -e $DEVDAX ]; then
		sudo $DAXCTL reconfigure-device --mode=devdax -f $DEVDAX
	fi
}
