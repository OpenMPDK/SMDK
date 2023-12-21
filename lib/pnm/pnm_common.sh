#!/bin/bash

readonly BASEDIR=$(readlink -f $(dirname $0))/../../
source "$BASEDIR/script/common.sh"

readonly SUCCESS=0
readonly ENV_SET_FAIL=1

DAXCTL="$BASEDIR/lib/cxl_cli/build/daxctl/daxctl"
DAXDEV="/dev/dax0.0"
PNM_CTL="$BASEDIR/lib/PNMLibrary-pnm-v3.0.0/PNMLibrary/build/tools/pnm_ctl"

function check_daxctl_exist() {
	if [ ! -e $DAXCTL ]; then
		log_error "daxctl does not exist. Run 'build_lib.sh cxl_cli' in /path/to/SMDK/lib"
		exit $ENV_SET_FAIL
	fi
}

function check_devdax_exist() {
	if [ ! -e $DAXDEV ]; then
		log_error "DAX device for PNM resource was not created"
		exit $ENV_SET_FAIL
	fi
}

function check_pnmctl_installed() {
	if [ ! -e $PNM_CTL ]; then
		log_error "pnm_ctl doesn't exist. Run 'build_lib.sh libpnm' in /path/to/SMDK/lib/."
		exit $ENV_SET_FAIL
	fi
}
