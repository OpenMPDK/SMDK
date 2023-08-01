#!/usr/bin/env bash

readonly BASEDIR=$(readlink -f $(dirname $0))/../../
source "$BASEDIR/script/common.sh"

BWDDIR=$BASEDIR/lib/bwd/
BWD_DD=$BWDDIR/drivers/bwd.ko
BWD_CONF=$BWDDIR/bwd.conf
BWD_DEV=/dev/bwd
BWD=$BWDDIR/bwd
BWDMAP=/run/bwd

if [ `whoami` != 'root' ]; then
	log_error "This script requires root privileges"
	exit 1
fi

if [ ! -e $BWD_CONF ]; then
	log_error "BWD configuration does not exist. Check /path/to/smdk/lib/bwd."
	exit 1
fi

if [ ! -e $BWD ]; then
	log_error "BWD binary does not exist. Run /path/to/smdk/lib/build_lib.sh bwd."
	exit 1
fi

if [ ! -e $BWD_DD ]; then
	log_error "BWD device driver file does not exist. Run /path/to/smdk/lib/build_lib.sh bwd."
	exit 1
fi

if [ -z `lsmod | grep bwd | awk '{print $1}'` ]; then
	log_normal "[insmod bwd module]"
	insmod $BWD_DD
	ret=$?

	if [ $ret = 0 ]; then
		log_normal "[insmod bwd module]..success"
	else
		log_error "[rmmod bwd module]..error"
		exit 1
	fi
fi

if [ ! -e $BWD_DEV ]; then
	log_error "BWD char device does not exist. BWD device driver load failed."
	exit 1
fi

echo -e "\n=====  BWD configure  =====\n`cat $BWD_CONF`"

# To run MLC, need to set hugepages. 4000 is recommended value of MLC.
NR_HP=$(cat /proc/sys/vm/nr_hugepages)
if [ $NR_HP -eq 0 ]; then
	echo 4000 > /proc/sys/vm/nr_hugepages
fi;
$BWD -c $BWD_CONF
