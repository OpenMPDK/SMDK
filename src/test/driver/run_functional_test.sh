#!/bin/bash
deviceId=0
DEVICE_PATH=/sys/kernel/cxl/devices/cxl$deviceId
DEVINCE_CNT=`ls -al /sys/kernel/cxl/devices/ | grep cxl | wc -l`

readonly BASEDIR=$(readlink -f $(dirname $0))/../../../
source "$BASEDIR/script/common.sh"

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

if [ ! -d "$DEVICE_PATH" ]; then
    log_error "kmem-ext module should be installed."
    exit $ENV_SET_FAIL
fi

print_buddyinfo
print_deviceinfo
echo

#offline test
echo [OFFLINE TEST]
state=`cat $DEVICE_PATH/state`
if [ $state == "online" ]; then
    old_node_id=`cat $DEVICE_PATH/node_id`
    echo -1 > $DEVICE_PATH/node_id
    new_state=`cat $DEVICE_PATH/state`
    if [ $new_state == "offline" ]; then
        print_buddyinfo
        print_deviceinfo
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

echo

#online test
state=`cat $DEVICE_PATH/state`
echo [ONLINE TEST]
if [ $state == "offline" ]; then
    echo $old_node_id > $DEVICE_PATH/node_id
    new_state=`cat $DEVICE_PATH/state`
    if [ $new_state == "online" ]; then
        print_buddyinfo
        print_deviceinfo
        echo "PASS"
    else
        echo "FAIL"
        exit 1
    fi
else
    #never reached here
    echo "Device is not offline. state: $state"
    exit 1
fi

echo

#node change test
echo [NODE CHANGE TEST]

if [ $DEVINCE_CNT -ge 2 ]; then
    cxl1_node_id=`cat /sys/kernel/cxl/devices/cxl1/node_id`
    echo $cxl1_node_id > $DEVICE_PATH/node_id
    new_nodeid=`cat $DEVICE_PATH/node_id`
    if [ $new_nodeid == $cxl1_node_id ]; then
        print_buddyinfo
        print_deviceinfo
        echo "PASS"
    else
        echo "FAIL"
        exit 1
    fi
fi

echo

#kobject release test
echo [KOBJECT RELEASE TEST]
SYSFS_PATH=/sys/kernel/cxl
KERNEL_NAME=`uname -r`

devdax=()
for path in $DEVICE_PATH/dax*; do
    name=$(basename $path)
    devdax+=($name)
done

# bind to device_dax driver
for name in ${devdax[@]}; do
    echo $name > /sys/bus/dax/devices/$name/driver/remove_id
    echo $name > /sys/bus/dax/devices/$name/driver/unbind
    echo $name > /sys/bus/dax/drivers/device_dax/new_id
    echo $name > /sys/bus/dax/drivers/device_dax/bind > /dev/null 2>&1
done

# remove kmem and check sysfs path
rmmod kmem
if [ $? == "0" ]; then
    if [ ! -d "$SYSFS_PATH" ]; then
        echo "kobject is released"
        echo "PASS"
        insmod /lib/modules/$KERNEL_NAME/kernel/drivers/dax/kmem.ko
    else
        echo "koject release is failed"
        echo "FAIL"
        insmod /lib/modules/$KERNEL_NAME/kernel/drivers/dax/kmem.ko
        exit 1
    fi
else
    echo "rmmod fail"
    exit 1
fi

echo

# bind to kmem driver
for name in ${devdax[@]}; do
    echo $name > /sys/bus/dax/devices/$name/driver/remove_id
    echo $name > /sys/bus/dax/devices/$name/driver/unbind
    echo $name > /sys/bus/dax/drivers/kmem/new_id
    echo $name > /sys/bus/dax/drivers/kmem/bind > /dev/null 2>&1
done

#check symlink 
echo [SYMLINK CHECK]
memdevs=($DEVICE_PATH/mem*)
if [ ${#memdevs[@]} -gt 0 ] && [ -e "${memdevs[0]}" ]; then
	for memdev_path in "${memdevs[@]}"; do
		if [ -d "$memdev_path" ]; then
			echo "memdev: $(readlink -f $memdev_path)"
			echo "PASS"
		else
			echo "memdev: not exist"
			exit 1
		fi
		break
	done
fi

exit 0
