#!/bin/bash

# Script for updating SMDK files in qemu rootfs.
# Prerequisite: SMDK archive (or files you want to update to qemu rootfs)
#   Or you can use scp, etc., to move the files directly to qemu virtual
#   machine while it is running.

QEMU_ROOTFS=/path/to/qemu-image.img
SMDK_ARCHIVE=/path/to/SMDK.tgz

sudo mkdir -p tmp
sudo mount -o loop $QEMU_ROOTFS tmp
sudo tar -xvf $SMDK_ARCHIVE -C tmp
sync
sudo umount tmp
