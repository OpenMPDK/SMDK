#!/usr/bin/env bash

readonly BASEDIR=$(readlink -f $(dirname $0))/../../
source "$BASEDIR/script/common.sh"

TIERDDIR=$BASEDIR/lib/tierd/
TIERD_DD=$BASEDIR/lib/linux-6.6-smdk/drivers/dax/kmem.ko
TIERD_CONF=$TIERDDIR/tierd.conf
TIERD_DEV=/dev/tierd
TIERD=$TIERDDIR/tierd
TIERDMAP=/run/tierd

if [ `whoami` != 'root' ]; then
	log_error "This script requires root privileges"
	exit 1
fi

if [ ! -e $TIERD_CONF ]; then
	log_error "TIERD configuration does not exist. Check /path/to/smdk/lib/tierd."
	exit 1
fi

if [ ! -e $TIERD ]; then
	log_error "TIERD binary does not exist. Run /path/to/smdk/lib/build_lib.sh tierd."
	exit 1
fi

if [ ! -e $TIERD_DD ]; then
	log_error "TIERD device driver file does not exist. Run /path/to/smdk/lib/build_lib.sh kernel."
	exit 1
fi

if [ -z `lsmod | awk '{print $1}' | grep "\<kmem\>"` ]; then
	log_normal "[insmod tierd module]"
	insmod $TIERD_DD
	ret=$?

	if [ $ret = 0 ]; then
		log_normal "[insmod tierd module]..success"
	else
		log_error "[rmmod tierd module]..error"
		exit 1
	fi
fi

if [ ! -e $TIERD_DEV ]; then
	log_error "TIERD char device does not exist. TIERD device driver load failed."
	exit 1
fi

echo -e "\n=====  TIERD configure  =====\n`cat $TIERD_CONF`"

# To run MLC, need to set hugepages. 4000 is recommended value of MLC.
NR_HP=$(cat /proc/sys/vm/nr_hugepages)
if [ $NR_HP -eq 0 ]; then
	echo 4000 > /proc/sys/vm/nr_hugepages
fi;
$TIERD -c $TIERD_CONF
