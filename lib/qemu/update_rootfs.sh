#!/bin/bash
#prerequisite : create dmdk-YYYY_MMDD_HHMM.v0.1.tgz by running build_lib.sh release
QEMU_ROOTFS=/var/www/html/qemu-image.img
DMDK_BIN=/root/DMDK/lib/dmdk.cxlmalloc-2021_0507_1622.v0.1.tgz

sudo mkdir -p tmp
sudo mount -o loop $QEMU_ROOTFS tmp
sudo tar -xvf $DMDK_BIN -C tmp
sync
sudo umount tmp
