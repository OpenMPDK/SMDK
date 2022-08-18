#!/usr/bin/bash
deviceId=0
DEVICE_PATH=/sys/kernel/cxl/devices/cxl$deviceId

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

print_buddyinfo
print_deviceinfo
echo

#offline test
echo [OFFLINE TEST]
state=$(cat $DEVICE_PATH/state)
if [ $state == "online" ]; then
    echo -1 > $DEVICE_PATH/node_id
    new_state=`cat $DEVICE_PATH/state`
    if [ $new_state == "offline" ]; then
        print_buddyinfo
        print_deviceinfo
        echo "PASS"
    else
        echo "FAIL"
        exit
    fi
else
    echo "Device is not online. state: $state"
    exit
fi

echo

#online test
state=`cat $DEVICE_PATH/state`
echo [ONLINE TEST]
if [ $state == "offline" ]; then
    echo 0 > $DEVICE_PATH/node_id
    new_state=`cat $DEVICE_PATH/state`
    if [ $new_state == "online" ]; then
        print_buddyinfo
        print_deviceinfo
        echo "PASS"
    else
        echo "FAIL"
        exit
    fi
else
    echo "Device is not offline. state: $state"
    exit
fi

echo

#node change test
echo [NODE CHANGE TEST]

echo 1 > $DEVICE_PATH/node_id
new_nodeid=`cat $DEVICE_PATH/node_id`
if [ $new_nodeid == "1" ]; then
    print_buddyinfo
    print_deviceinfo
    echo "PASS"
else
    echo "FAIL"
    exit
fi

#kobject release test
echo [KOBJECT RELEASE TEST]
SYSFS_PATH=/sys/kernel/cxl
KERNEL_NAME=`uname -r`
rmmod cxl_pci
if [ $? == "0" ]; then
        if [ ! -d "$SYSFS_PATH" ]; then
                echo "kobject is released"
                echo "PASS"
                insmod /lib/modules/$KERNEL_NAME/kernel/drivers/cxl/cxl_pci.ko
        else
                echo "koject release is failed"
                echo "FAIL"
                insmod /lib/modules/$KERNEL_NAME/kernel/drivers/cxl/cxl_pci.ko
                exit
        fi
else
        echo "rmmod fail"
        exit
fi

#check symlink 
for memdev_path in $DEVICE_PATH/mem*; do
	if [ -d "$memdev_path" ]; then
		echo "memdev: $(readlink -f $memdev_path)"
	else
		echo "memdev: not exist"
	fi
	break
done
