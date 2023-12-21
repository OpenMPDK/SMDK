#!/usr/bin/env bash

readonly PNMSCRIPTDIR=$(readlink -f $(dirname $0))
source "$PNMSCRIPTDIR/pnm_common.sh"

function install_imdb_resource() {
	check_daxctl_exist

	if [ -n "$(lsmod | grep imdbcxl)" ]; then 
		log_normal "IMDB resource module is already installed"
	else
 		sudo modprobe -v imdbcxl
		if [ $? -ne 0 ]; then
			log_error "IMDB resource module install failed"
			exit $ENV_SET_FAIL
		fi
	fi

	check_devdax_exist

	sleep 1

	sudo $DAXCTL reconfigure-device --mode=devdax -f dax0.0	
	if [ $? -ne 0 ]; then
		log_error "daxctl reconfigure for IMDB resource using failed"
		exit $ENV_SET_FAIL
	fi

        log_normal "IMDB resource install done"
        exit $SUCCESS
}

function cleanup_imdb_resource() {
	if [ -n "$(lsmod | grep imdb)" ]; then
		sudo rmmod imdbcxl
		sudo rmmod imdb_resource
	else
	        log_normal "IMDB resource is already cleaned up"
	fi

        log_normal "IMDB resource uninstall done"
        exit $SUCCESS
}

case "$1" in
	"--install"|"-i")
		install_imdb_resource
		;;
	"--uninstall"|"-u")
		cleanup_imdb_resource
		;;
	*)
		echo "Usage: setup_imdb_hw.sh <command>

	<command>				<description>

	--install(-i)				Setup resources needed for PNM IMDB HW.
	--uninstall(-u)				Cleanup resources needed for PNM IMDB HW."
		;;
esac
