#!/bin/bash

#Change the number of devces and address of devices according to your system
NUM_DEVICE=3
ADDRESS=("1080000000-307fffffff" "3080000000-507fffffff" "5080000000-707fffffff")

#Download fio from https://github.com/axboe/fio.git
#Change FIO_PATH from /path/to to your system's path
FIO_PATH=/path/to/fio/

function print_result(){
	echo IOMEM 
	cat /proc/iomem | grep -A 6 hmem
	echo
	echo [[Buddy Info]]	
	cat /proc/buddyinfo
	echo
	echo -----------------------------------------------------------------
}

if [ ! -d $FIO_PATH ]; then
	echo "Change FIO PATH"
	exit
fi

if [ `whoami` != 'root' ]; then
	echo "This test requires root privileges"
	exit
fi

for ((i=0 ; i < $NUM_DEVICE ; i ++)) do
	if [ ! -d "/sys/devices/platform/hmem.$i/dax$i.0" ]; then
	   	echo Check Device list
		exit
	fi
done

print_result

for cxldev in /sys/kernel/cxl/devices/cxl*; do
	echo -1 > $cxldev/node_id
done

for ((i=0 ; i < $NUM_DEVICE ; i ++)) do
	if [ ! -d "/sys/devices/platform/hmem.$i/dax$i.0/mapping0/" ]; then
		echo "${ADDRESS[$i]}" > /sys/devices/platform/hmem.$i/dax$i.0/mapping
	fi
	echo dax$i.0 > /sys/bus/dax/drivers/device_dax/bind
done

print_result

echo "FIO TEST"
$FIO_PATH/fio $FIO_PATH/examples/dev-dax.fio

for ((i=0 ; i < $NUM_DEVICE ; i ++)) do
	echo dax$i.0 > /sys/bus/dax/drivers/device_dax/unbind
done

for cxldev in /sys/kernel/cxl/devices/cxl*; do
	echo 0 > $cxldev/node_id
done

print_result
