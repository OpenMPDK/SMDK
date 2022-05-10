#!/bin/bash

source "../../script/common.sh"
SMDK_KERNEL_PATH=../linux-5.18-rc3-smdk
QEMU_BUILD_PATH=./qemu_cxl2.0v4/build
ROOTFS_PATH=.

QEMU_SYSTEM_BINARY=${QEMU_BUILD_PATH}/qemu-system-x86_64
BZIMAGE_PATH=${SMDK_KERNEL_PATH}/arch/x86_64/boot/bzImage
IMAGE_PATH=${ROOTFS_PATH}/qemu-image-1.img

if [ ! -f "${QEMU_SYSTEM_BINARY}" ]; then
	log_error "qemu-system-x86_64 is necessary. Run build_qemu.sh first."
	exit 1
fi

if [ ! -f "${BZIMAGE_PATH}" ]; then
	log_error "SMDK kernel image is necessary. Run build_lib.sh kernel first."
	exit 1
fi

if [ ! -f "${IMAGE_PATH}" ]; then
	log_error "QEMU rootfs is necessary. Run create_rootfs.sh or download it first."
	exit 1
fi
 
sudo ${QEMU_SYSTEM_BINARY} \
    -smp 6 \
    -numa node,cpus=0-5,memdev=mem0,nodeid=0 \
    -object memory-backend-ram,id=mem0,size=4G \
    -kernel ${BZIMAGE_PATH} \
    -drive file=${IMAGE_PATH},index=0,media=disk,format=raw \
    -enable-kvm \
    -monitor telnet::45451,server,nowait \
    -serial mon:stdio \
    -nographic \
    -append "root=/dev/sda rw console=ttyS0 nokaslr memblock=debug loglevel=7" \
    -m 4G \
    -net user,net=10.0.2.0/8,host=10.0.2.2,dhcpstart=10.0.2.21,hostfwd=tcp::2241-:22,hostfwd=tcp::6371-:6379,hostfwd=tcp::11211-:11211, \
    -net nic \
