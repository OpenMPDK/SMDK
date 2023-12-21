#!/bin/bash

readonly BASEDIR=$(readlink -f $(dirname $0))/../../..
source "$BASEDIR/script/common.sh"

#Change the list of dax devices according to your system
#
#Example for 2 CXL devices
#NUM_DEVICE=2
#DEVDAXS=("dax0.0" "dax1.0")
#
#If you are not sure about it, leave NUM_DEVICE as 0 to detect automatically
NUM_DEVICE=0
DEVDAXS=()
if [ $NUM_DEVICE -eq 0 ]; then
	for path in /sys/bus/dax/devices/dax*; do
		name=$(basename $path)
		DEVDAXS+=($name)
	done
fi

DAXCTL=$BASEDIR/lib/cxl_cli/build/daxctl/daxctl

#Download fio from https://github.com/axboe/fio.git
#Change FIO_PATH from /path/to to your system's path
FIO_PATH=/path/to/fio/

function print_result(){
	echo IOMEM 
	cat /proc/iomem | grep -A 6 hmem
	cat /proc/iomem | grep -A 5 "CXL Window"
	echo
	echo [[Buddy Info]]	
	cat /proc/buddyinfo
	echo
	echo -----------------------------------------------------------------
}

if [ ! -d $FIO_PATH ]; then
	echo "Change FIO PATH"
	exit 2
fi

if [ ! -f $DAXCTL ]; then
	echo daxctl does not exist. Run 'build_lib.sh cxl_cli' in /path/to/SMDK/lib
	exit 2
fi

if [ `whoami` != 'root' ]; then
	echo "This test requires root privileges"
	exit 2
fi

for devdax in "${DEVDAXS[@]}"
do
	if [ ! -d "/sys/bus/dax/devices/$devdax/" ]; then
	   	echo Check Device list
		exit 2
	fi
done

print_result

for devdax in "${DEVDAXS[@]}"
do
	$DAXCTL reconfigure-device --mode=devdax $devdax -f
	ret=$?
	if [ $ret != 0 ]; then
		echo Failed to reconfigure device $devdax
		exit 1
	fi
done

print_result

echo "FIO TEST"
$FIO_PATH/fio $FIO_PATH/examples/dev-dax.fio
ret=$?

for devdax in "${DEVDAXS[@]}"
do
	$DAXCTL reconfigure-device --mode=system-ram $devdax
	ret=$?
	if [ $ret != 0 ]; then
		echo Failed to reconfigure device $devdax
		exit 1
	fi
done

print_result

echo
if [ $ret == 0 ]; then
    echo "PASS"
else
    echo "FAIL"
    exit 1
fi

exit 0
