#!/usr/bin/env bash

readonly PNMSCRIPTDIR=$(readlink -f $(dirname $0))
source "$PNMSCRIPTDIR/pnm_common.sh"

function install_dlrm_fsim() {
	if [ -n "$(lsmod | grep sls_resource)" ]; then 
		log_normal "DLRM resource module is already installed"
	else
 		sudo modprobe -v sls_resource
		if [ $? -ne 0 ]; then
			log_error "DLRM resource module install failed"
			exit $ENV_SET_FAIL
		fi
	fi

	check_pnmctl_installed
	$PNM_CTL setup-shm --sls
	if [ $? -ne 0 ]; then
		log_error "pnm_ctl setup for dlrm fsim fail"
		exit $ENV_SET_FAIL
	fi

        log_normal "DLRM resource for fsim install done"
        exit $SUCCESS
}

function cleanup_dlrm_fsim() {
	check_pnmctl_installed
	$PNM_CTL destroy-shm --sls
	if [ $? -ne 0 ]; then
		log_error "pnm_ctl clenaup for dlrm fsim fail"
		exit $ENV_SET_FAIL
	fi

	if [ -n "$(lsmod | grep sls)" ]; then
		sudo rmmod cxlsls
		sudo rmmod sls_resource
	else
	        log_normal "DLRM resource is already cleaned up"
	fi

        log_normal "DLRM resource for fsim uninstall done"
        exit $SUCCESS
}

case "$1" in
	"--install"|"-i")
		install_dlrm_fsim
		;;
	"--uninstall"|"-u")
		cleanup_dlrm_fsim
		;;
	*)
		echo "Usage: setup_dlrm_fsim.sh <command>

	<command>				<description>

	--install(-i)				Setup resources needed for PNM DLRM Functional Simulator.
	--uninstall(-u)				Cleanup resources needed for PNM DLRM Functional Simulator."
		;;
esac
