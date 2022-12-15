#!/bin/bash
# prerequisite:
# 1. SMDK Kernel is running

deviceId=0
DEVICE_PATH=/sys/kernel/cxl/devices/cxl$deviceId

readonly BASEDIR=$(readlink -f $(dirname $0))/../../../
source "$BASEDIR/script/common.sh"

SCRIPT_PATH=$(readlink -f $(dirname $0))

#Change the address of devices according to your system
ADDRESS="1050000000-304fffffff"

function print_buddyinfo() {
	echo [[Buddy Info]]
	cat /proc/buddyinfo
}

function print_deviceinfo() {
	echo [[Device Info]]
	start_address=$(cat $DEVICE_PATH/start_address)
	echo start_address: $start_address
	size=`cat $DEVICE_PATH/size`
	echo size: $size
	node_id=`cat $DEVICE_PATH/node_id`
	echo node_id: $node_id
	socket_id=`cat $DEVICE_PATH/socket_id`
	echo socket_id: $socket_id
	state=`cat $DEVICE_PATH/state`
	echo state: $state
}

if [ `whoami` != 'root' ]; then
	log_error "This test requires root privileges"
	exit 2
fi

print_buddyinfo
print_deviceinfo
echo

echo [online rollback test]
state=`cat $DEVICE_PATH/state`
if [ $state == "online" ]; then
    $SCRIPT_PATH/mmap_cxl & sleep 0.3s; echo -1 > $DEVICE_PATH/node_id
    new_state=`cat $DEVICE_PATH/state`
    print_buddyinfo
    print_deviceinfo

    if [ $new_state == "online" ]; then
        echo "PASS"
    else
        echo "FAIL"
        exit 1
    fi
else
    echo "Device is not online. state: $state"
    echo "Try again after onlining CXL device"
    exit 2
fi

wait

echo
echo -1 > $DEVICE_PATH/node_id
state=`cat $DEVICE_PATH/state`
# not neseccary part
# for very rare case that device offline is not working?
if [ $state == "online" ]; then
    echo "Device is not offline. state: $state"
    echo "offline rollback test requires offlined device"
    exit 1
fi

print_buddyinfo
print_deviceinfo
echo

echo [offline rollback test]
echo $ADDRESS > /sys/devices/platform/hmem.0/dax0.0/mapping
echo dax0.0 > /sys/bus/dax/drivers/device_dax/bind

echo 0 > $DEVICE_PATH/node_id
state=`cat $DEVICE_PATH/state`
print_buddyinfo
print_deviceinfo

echo dax0.0 > /sys/bus/dax/drivers/device_dax/unbind
echo 0 > $DEVICE_PATH/node_id

if [ $state == "offline" ]; then
	echo "PASS"
else
	echo "FAIL"
	exit 1
fi

exit 0
