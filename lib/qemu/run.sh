#!/bin/bash

source "../../script/common.sh"
SMDK_KERNEL_PATH=../linux-5.17-rc5-smdk
QEMU_BUILD_PATH=./qemu_cxl2.0v4/build
ROOTFS_PATH=.
MONITOR_PORT=45454

QEMU_SYSTEM_BINARY=${QEMU_BUILD_PATH}/qemu-system-x86_64
BZIMAGE_PATH=${SMDK_KERNEL_PATH}/arch/x86_64/boot/bzImage
IMAGE_PATH=${ROOTFS_PATH}/qemu-image.img

function print_usage(){
	echo ""
	echo "Usage:"
	echo " $0 [-x vm_index(0-9)]"
	echo ""
}

while getopts "x:" opt; do
	case "$opt" in
		x)
			if [ $OPTARG -lt 0 ] || [ $OPTARG -gt 9 ]; then
				echo "Error: VM count should be 0-9"
				exit 1
			fi
			VMIDX=$OPTARG
			;;
		*)
			print_usage
			exit 1
			;;
	esac
done

if [ -z ${VMIDX} ]; then
	NET_OPTION="-net user,hostfwd=tcp::2242-:22,hostfwd=tcp::6379-:6379,hostfwd=tcp::11211-:11211, -net nic"
else
	echo "Info: Running VM #${VMIDX}..."
	MONITOR_PORT="4545${VMIDX}"
	IMAGE_PATH=$(echo ${IMAGE_PATH} | sed 's/.img/-'"${VMIDX}"'.img/')
	MACADDR="52:54:00:12:34:${VMIDX}${VMIDX}"
	TAPNAME="tap${VMIDX}"
	NET_OPTION="-net nic,macaddr=${MACADDR} -net tap,ifname=${TAPNAME},script=no"

	IFCONFIG_TAPINFO=`ifconfig | grep ${TAPNAME}`
	if [ -z "${IFCONFIG_TAPINFO}" ]; then
		log_error "${TAPNAME} SHOULD be up for using network in VM. Run ./setup_bridge.sh first"
		exit 1
	fi
fi

if [ ! -f "${QEMU_SYSTEM_BINARY}" ]; then
	log_error "qemu-system-x86_64 is necessary. Run build_qemu.sh first."
	exit 1
fi

if [ ! -f "${BZIMAGE_PATH}" ]; then
	log_error "SMDK kernel image is necessary. Run build_lib.sh kernel first."
	exit 1
fi

if [ ! -f "${IMAGE_PATH}" ]; then
	log_error "QEMU rootfs ${IMAGE_PATH} is necessary. Run create_rootfs.sh or download it first."
	exit 1
fi
 
sudo ${QEMU_SYSTEM_BINARY} \
    -smp 6 \
    -numa node,cpus=0-2,memdev=mem0,nodeid=0 \
    -object memory-backend-ram,id=mem0,size=8G \
    -numa node,cpus=3-5,memdev=mem1,nodeid=1 \
    -object memory-backend-ram,id=mem1,size=8G \
    -kernel ${BZIMAGE_PATH} \
    -drive file=${IMAGE_PATH},index=0,media=disk,format=raw \
    -enable-kvm \
    -monitor telnet::${MONITOR_PORT},server,nowait \
    -serial mon:stdio \
    -nographic \
    -append "root=/dev/sda rw console=ttyS0 nokaslr memblock=debug loglevel=7" \
    -m 16G,slots=4,maxmem=32G \
    ${NET_OPTION}
