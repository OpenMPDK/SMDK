#!/bin/bash

readonly BASEDIR=$(readlink -f $(dirname $0))/../../

source "${BASEDIR}/script/common.sh"

QEMU_PATH=${BASEDIR}/lib/qemu/
QEMU_BUILD_PATH=${QEMU_PATH}/qemu-8.1.50/build/
IMAGE_NAME=qemu-image.img

if [ ! -f "${QEMU_BUILD_PATH}/qemu-img" ]; then
	log_error "qemu-img binary does not exist. Run 'build_lib.sh qemu' in /path/to/SMDK/lib/"
	exit 2
fi

${QEMU_BUILD_PATH}/qemu-img create ${QEMU_PATH}/${IMAGE_NAME} 10g
mkfs.ext4 ${QEMU_PATH}/${IMAGE_NAME}
mkdir -p ${QEMU_PATH}/mount-point.dir
sudo mount -o loop ${QEMU_PATH}/${IMAGE_NAME} ${QEMU_PATH}/mount-point.dir/
sudo apt install debootstrap debian-keyring debian-archive-keyring
# When debootstrap install if wget error occurs,
# add proxy configuration to /etc/wgetrc (http_proxy, https_proxy, ftp_proxy)
sudo debootstrap --no-check-certificate --arch amd64 jessie ${QEMU_PATH}/mount-point.dir/
mkdir ${QEMU_PATH}/mnt
sudo mount ${IMAGE_NAME} ${QEMU_PATH}/mnt
cd ${QEMU_PATH}/mnt
sudo chroot .

# After creating rootfs, 
# 1) Change root password
# $ passwd

# 2) (Optional) Add proxy configuration
# Open /etc/profile, and add proxy configuration
# export HTTP_PROXY="http://<PROXY_IP>:<PROXY_PORT>/"
# export HTTPS_PROXY="http://<PROXY_IP>:<PROXY_PORT>/"
# $ sh /etc/profile

# 3) (Optional) Add local Ubuntu repository mirrors
# Open /etc/apt/sources.list, and add repository mirrors

# 4) Package update
# $ apt update
# $ apt upgrade
# $ apt install vim pciutils debian-keyring debian-archive-keyring openssh-server

# 5) Modify sshd config
# $ vi /etc/ssh/sshd_config
# ...
# PermitRootLogin yes
# ...

# 6) Modify network config
# $ vi /etc/network/interfaces
# # add below lines
# auto ens3
# iface ens3 inet dhcp
# # for q35 machine
# auto enp0s2
# iface enp0s2 inet dhcp

# 7) Quit
# $ exit
