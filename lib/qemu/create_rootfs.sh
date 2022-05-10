#!/bin/bash

source "../../script/common.sh"
QEMU_BUILD_PATH=./qemu_cxl2.0v4/build/
IMAGE_NAME=qemu-image.img

if [ ! -f "${QEMU_BUILD_PATH}/qemu-img" ]; then
	log_error "qemu-img binary is necessary. Run build_qemu.sh first."
	exit 1
fi

${QEMU_BUILD_PATH}/qemu-img create ./${IMAGE_NAME} 10g
mkfs.ext4 ./${IMAGE_NAME}
mkdir -p mount-point.dir
sudo mount -o loop ./${IMAGE_NAME} ./mount-point.dir/
sudo apt install debootstrap debian-keyring debian-archive-keyring
# When debootstrap install if wget error occurs,
# add proxy configuration to /etc/wgetrc (http_proxy, https_proxy, ftp_proxy)
sudo debootstrap --no-check-certificate --arch amd64 jessie ./mount-point.dir/
mkdir mnt
sudo mount ${IMAGE_NAME} ./mnt
cd mnt
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

# 7) Quit
# $ exit
