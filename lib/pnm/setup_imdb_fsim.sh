#!/usr/bin/env bash

readonly PNMSCRIPTDIR=$(readlink -f $(dirname $0))
source "$PNMSCRIPTDIR/pnm_common.sh"

function install_imdb_fsim() {
	if [ -n "$(lsmod | grep imdb_resource)" ]; then 
		log_normal "IMDB resource module is already installed"
	else
 		sudo modprobe -v imdb_resource
		if [ $? -ne 0 ]; then
			log_error "IMDB resource module install failed"
			exit $ENV_SET_FAIL
		fi
	fi

	check_pnmctl_installed
	$PNM_CTL setup-shm --imdb
	if [ $? -ne 0 ]; then
		log_error "pnm_ctl setup for imdb fsim fail"
		exit $ENV_SET_FAIL
	fi

        log_normal "IMDB resource for fsim install done"
        exit $SUCCESS
}

function cleanup_imdb_fsim() {
	check_pnmctl_installed
	$PNM_CTL destroy-shm --imdb
	if [ $? -ne 0 ]; then
		log_error "pnm_ctl clenaup for imdb fsim fail"
		exit $ENV_SET_FAIL
	fi

	if [ -n "$(lsmod | grep imdb)" ]; then
		sudo rmmod imdbcxl
		sudo rmmod imdb_resource
	else
	        log_normal "IMDB resource is already cleaned up"
	fi

        log_normal "IMDB resource for fsim uninstall done"
        exit $SUCCESS
}

case "$1" in
	"--install"|"-i")
		install_imdb_fsim
		;;
	"--uninstall"|"-u")
		cleanup_imdb_fsim
		;;
	*)
		echo "Usage: setup_imdb_fsim.sh <command>

	<command>				<description>

	--install(-i)				Setup resources needed for PNM IMDB Functional Simulator.
	--uninstall(-u)				Cleanup resources needed for PNM IMDB Functional Simulator."
		;;
esac
