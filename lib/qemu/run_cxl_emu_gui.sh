#!/bin/bash

readonly BASEDIR=$(readlink -f $(dirname $0))/../../

source "${BASEDIR}/script/common.sh"

QEMU_PATH=${BASEDIR}/lib/qemu/
QEMU_BUILD_PATH=${QEMU_PATH}/qemu-7.1.0/build/
IMAGE_PATH=${QEMU_PATH}/qemu-image-gui.img
MONITOR_PORT=45454

QEMU_SYSTEM_BINARY=${QEMU_BUILD_PATH}/qemu-system-x86_64

if [ ! -f "${QEMU_SYSTEM_BINARY}" ]; then
    log_error "qemu-system-x86_64 binary does not exist. Run 'build_lib.sh qemu' in /path/to/SMDK/lib/"
    exit 2
fi

if [ ! -f "${IMAGE_PATH}" ]; then
    log_error "QEMU image ${IMAGE_PATH} does not exist. Run 'create_gui_image.sh' and 'gui_setup_ssh.sh' in /path/to/SMDK/lib/"
    exit 2
fi

sudo ${QEMU_SYSTEM_BINARY} \
    -smp 6 \
    -enable-kvm \
    -m 8G,slots=4,maxmem=16G \
    -drive file=${IMAGE_PATH},format=qcow2 \
    -serial mon:stdio \
    -net nic \
    -net user,hostfwd=tcp::2242-:22 \
    -monitor telnet::${MONITOR_PORT},server,nowait \
    -machine q35,cxl=on \
    -M cxl-fmw.0.targets.0=cxl.1,cxl-fmw.0.size=4G \
    -device pxb-cxl,bus_nr=12,bus=pcie.0,id=cxl.1 \
    -device cxl-rp,port=0,bus=cxl.1,id=root_port13,chassis=0,slot=2 \
    -object memory-backend-file,id=cxl-mem1,share=on,mem-path=/tmp/cxltest.raw,size=256M \
    -object memory-backend-file,id=cxl-lsa1,share=on,mem-path=/tmp/lsa.raw,size=256M \
    -device cxl-type3,bus=root_port13,memdev=cxl-mem1,lsa=cxl-lsa1,id=cxl-mem0
