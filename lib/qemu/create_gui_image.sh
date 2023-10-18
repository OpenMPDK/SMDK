#!/bin/bash
# 
# Script for creating QEMU Ubuntu image based on GUI interface
# System will be booted by ubuntu.iso image due to "-boot d" option
#
# Prerequisite: Ubuntu image file, VNC viewer

readonly BASEDIR=$(readlink -f $(dirname $0))/../../

source "${BASEDIR}/script/common.sh"

QEMU_PATH=${BASEDIR}/lib/qemu/
QEMU_BUILD_PATH=${QEMU_PATH}/qemu-8.1.50/build/
IMAGE_FILENAME=qemu-image-gui.img
IMAGE_PATH=${QEMU_PATH}/${IMAGE_FILENAME}
UBUNTU_ISO=/path/to/ubuntu.iso

QEMU_SYSTEM_BINARY=${QEMU_BUILD_PATH}/qemu-system-x86_64

if [ ! -f "${QEMU_BUILD_PATH}/qemu-img" ]; then
	log_error "qemu-img binary does not exist. Run 'build_lib.sh qemu' in /path/to/SMDK/lib/"
	exit 2
fi

if [ ! -f ${UBUNTU_ISO} ]; then
	log_error "Ubuntu image does not exist. Download image file and update file path"
	exit 2
fi

if [ -f ${IMAGE_PATH} ]; then
	log_error "Image file ${IMAGE_FILENAME} already exists. Back up or delete the file, and try again."
	exit 2
fi

${QEMU_BUILD_PATH}/qemu-img create -f qcow2 ${IMAGE_PATH} 64g

sudo ${QEMU_SYSTEM_BINARY} \
    -smp 6 \
    -enable-kvm \
    -m 8G,slots=4,maxmem=16G \
    -drive if=virtio,file=${IMAGE_PATH},format=qcow2,cache=none \
    -machine q35,cxl=on \
    -cdrom ${UBUNTU_ISO} \
    -boot d

# After QEMU turns on, connect to it with VNC viewer
# 	default address: 127.0.0.1:5900 (localhost:5900)
#
# Follow the installation process and turn off QEMU when it is done

