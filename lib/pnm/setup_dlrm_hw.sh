#!/usr/bin/env bash

readonly PNMSCRIPTDIR=$(readlink -f $(dirname $0))
source "$PNMSCRIPTDIR/pnm_common.sh"

function install_dlrm_resource() {
	check_daxctl_exist

	if [ -n "$(lsmod | grep slscxl)" ]; then 
		log_normal "DLRM resource module is already installed"
	else
 		sudo modprobe -v slscxl
		if [ $? -ne 0 ]; then
			log_error " resource module install failed"
			exit $ENV_SET_FAIL
		fi
	fi

	check_devdax_exist
	sleep 1

	sudo $DAXCTL reconfigure-device --mode=devdax -f dax0.0	
	if [ $? -ne 0 ]; then
		log_error "daxctl reconfigure for DLRM resource using failed"
		exit $ENV_SET_FAIL
	fi

        log_normal "DLRM resource install done"
        exit $SUCCESS
}

function cleanup_dlrm_resource() {
	if [ -n "$(lsmod | grep sls)" ]; then
		sudo rmmod slscxl
		sudo rmmod sls_resource
	else
	        log_normal "DLRM resource is already cleaned up"
	fi

        log_normal "DLRM resource uninstall done"
        exit $SUCCESS
}

case "$1" in
	"--install"|"-i")
		install_dlrm_resource
		;;
	"--uninstall"|"-u")
		cleanup_dlrm_resource
		;;
	*)
		echo "Usage: setup_imdb_hw.sh <command>

	<command>				<description>

	--install(-i)				Setup resources needed for PNM DLRM HW.
	--uninstall(-u)				Cleanup resources needed for PNM DLRM HW."
		;;
esac
