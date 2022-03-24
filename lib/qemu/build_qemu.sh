#!/bin/bash

QEMU_PATH=./qemu_cxl2.0v4/
BUILD_PATH=${QEMU_PATH}/build/
CURRENT_PATH=`pwd`

mkdir -p ${BUILD_PATH}
cd ${BUILD_PATH}
../configure --target-list=x86_64-softmmu --enable-debug
make
cd ${CURRENT_PATH}
