# prerequisite:
# 1. SMDK Kernel is running
#!/usr/bin/env bash

deviceId=0
DEVICE_PATH=/sys/kernel/cxl/devices/cxl$deviceId

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
	echo "This test requires root privileges"
	exit
fi

echo [online rollback test]
print_deviceinfo
echo -1 > $DEVICE_PATH/node_id & sleep 1s; ./mmap_cxl
print_deviceinfo

echo -1 > $DEVICE_PATH/node_id

echo
echo [offline rollback test]
echo $ADDRESS > /sys/devices/platform/hmem.0/dax0.0/mapping
echo dax0.0 > /sys/bus/dax/drivers/device_dax/bind

print_deviceinfo
echo 0 > $DEVICE_PATH/node_id
print_deviceinfo

state=`cat $DEVICE_PATH/state`
if [ $state == "offline" ]; then
	echo "PASS"
else
	echo "FAIL"
fi

echo dax0.0 > /sys/bus/dax/drivers/device_dax/unbind
echo 0 > $DEVICE_PATH/node_id
