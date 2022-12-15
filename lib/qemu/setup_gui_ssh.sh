#!/bin/bash
# 
# Script for QEMU machine based on GUI interface
# Setup package installation environment and download openssh-server to image created by create_gui_image.sh
# After openssh-server is installed, connection by ssh is possible.
#
# Prerequisite: VNC viewer

readonly BASEDIR=$(readlink -f $(dirname $0))/../../

source "${BASEDIR}/script/common.sh"

QEMU_PATH=${BASEDIR}/lib/qemu/
QEMU_BUILD_PATH=${QEMU_PATH}/qemu-7.1.0/build/
IMAGE_PATH=${QEMU_PATH}/qemu-image-gui.img

QEMU_SYSTEM_BINARY=${QEMU_BUILD_PATH}/qemu-system-x86_64

if [ ! -f "${QEMU_SYSTEM_BINARY}" ]; then
    log_error "qemu-system-x86_64 binary does not exist. Run 'build_lib.sh qemu' in /path/to/SMDK/lib/"
    exit 2
fi

if [ ! -f "${IMAGE_PATH}" ]; then
    log_error "QEMU image ${IMAGE_PATH} does not exist. Run 'create_gui_image.sh' in /path/to/SMDK/lib/"
    exit 2
fi

sudo ${QEMU_SYSTEM_BINARY} \
    -smp 6 \
    -enable-kvm \
    -m 8G,slots=4,maxmem=16G \
    -drive if=virtio,file=${IMAGE_PATH},format=qcow2 \
    -serial mon:stdio \
    -net nic \
    -net user,hostfwd=tcp::2242-:22 \
    -machine q35,cxl=on 

# After QEMU turns on, connect to it with VNC viewer
# 	default address: 127.0.0.1:5900 (localhost:5900)
#
# After installing ubuntu,
#
# 1) (Optional) Add proxy configuration
# Open /etc/profile, and add proxy configuration
# export HTTP_PROXY="http://<PROXY_IP>:<PROXY_PORT>/"
# export HTTPS_PROXY="http://<PROXY_IP>:<PROXY_PORT>/"
# $ sh /etc/profile
#
# 2) (Optional) Add local Ubuntu repository mirrors
# Open /etc/apt/sources.list, and add repository mirrors
#
# 3) Package update
# $ apt update
# $ apt upgrade
# $ apt install vim pciutils debian-keyring debian-archive-keyring openssh-server
#
# After installing openssh-server, you can connect to QEMU by ssh
# $ ssh localhost -p 2242 -l <hostname>

