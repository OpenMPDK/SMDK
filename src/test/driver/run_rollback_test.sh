#!/bin/bash
# prerequisite:
# 1. SMDK Kernel is running

deviceId=0
DEVICE_PATH=/sys/kernel/cxl/devices/cxl$deviceId

readonly BASEDIR=$(readlink -f $(dirname $0))/../../../
source "$BASEDIR/script/common.sh"

SCRIPT_PATH=$(readlink -f $(dirname $0))

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

# unbind from kmem
old_node_id=`cat $DEVICE_PATH/node_id`
devdax=()
for path in $DEVICE_PATH/dax*; do
    name=$(basename $path)
    devdax+=($name)
done

for name in ${devdax[@]}; do
    echo $name > /sys/bus/dax/devices/$name/driver/remove_id
    echo $name > /sys/bus/dax/devices/$name/driver/unbind
done

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
# 1) bind to device_dax and try to change node_id
for name in ${devdax[@]}; do
    echo $name > /sys/bus/dax/drivers/device_dax/new_id
    echo $name > /sys/bus/dax/drivers/device_dax/bind > /dev/null 2>&1
done

echo $old_node_id > $DEVICE_PATH/node_id
state=`cat $DEVICE_PATH/state`
print_buddyinfo
print_deviceinfo

# 2) unbind from device_dax and try to change node_id
for name in ${devdax[@]}; do
    echo $name > /sys/bus/dax/devices/$name/driver/remove_id
    echo $name > /sys/bus/dax/devices/$name/driver/unbind
done
echo $old_node_id > $DEVICE_PATH/node_id

# 3) check result
if [ $state == "offline" ]; then
	echo "PASS"
else
	echo "FAIL"
	exit 1
fi

# bind to kmem driver
for name in ${devdax[@]}; do
    echo $name > /sys/bus/dax/drivers/kmem/new_id
    echo $name > /sys/bus/dax/drivers/kmem/bind > /dev/null 2>&1
done

exit 0
