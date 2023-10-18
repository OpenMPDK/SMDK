#!/usr/bin/env bash
# the script targets ubuntu and debian distos 
# the script needs to be run only once on a system being newly installed 

# Minimal install
apt-get install -y gcc g++ make libcunit1-dev libaio-dev libssl-dev 
apt-get install -y uuid-dev libiscsi-dev python libncurses5-dev libncursesw5-dev python3-pip
apt-get install -y autoconf automake libtool help2man

# Developer tools
apt-get install -y git astyle pep8 lcov clang sg3-utils pciutils shellcheck abigail-tools ctags

# libnuma
apt-get install -y libnuma-dev nasm meson numactl

# python style checker not available on ubuntu 16.04 or earlier.
apt-get install -y pycodestyle || true
apt-get install -y python3-configshell-fb python3-pexpect 
apt-get install -y python3-paramiko

# building docs
apt-get install -y doxygen mscgen graphviz

# CUnit and ncurses mode
apt-get install -y libcunit1-ncurses-dev

# bazel
apt-get install -y openjdk-8-jdk openjdk-11-jdk

# SMDK
apt-get install -y dialog
apt-get install -y flex bison
apt-get install -y libelf-dev zstd

# memcached
apt-get install -y libevent-dev

# qemu
apt-get install -y libpixman-1-dev cgdb
apt-get install -y openvpn
apt-get install -y pkg-config libglib2.0-dev
apt-get install -y libslirp-dev

# dax
apt-get install -y libpmem-dev

# ACPI Tables
apt-get install -y acpica-tools

# java
apt-get install -y libx11-dev libxext-dev libxrender-dev libxrandr-dev libxtst-dev libxt-dev
apt-get install -y libcups2-dev libfontconfig1-dev libasound2-dev
apt-get install -y cmake junit4

# voltdb
apt-get install -y ant ant-optional

# opt_api python binding
apt-get install -y python-dev python3-dev libffi-dev
python3 -m pip install cffi

# meson
apt-get install -y libiniparser1 libiniparser-dev libkmod-dev libkeyutils-dev
apt-get install -y libjson-c3 libjson-c-dev libudev1 libudev-dev ninja-build meson

# cxl_cli
apt-get install -y libiniparser1 libiniparser-dev libkmod-dev libkeyutils-dev
apt-get install -y libjson-c3 libjson-c-dev libudev1 libudev-dev ninja-build asciidoctor meson
apt-get install -y libtracefs-dev libtracefs1 libtraceevent-dev libtraceevent1 #ubuntu 22.04+

# docker
# Two ways to install Docker Engine
# 1) Install static binaries: https://docs.docker.com/engine/install/binaries/
# 2) Install the engine from Docker repository: https://docs.docker.com/engine/install/ubuntu/ (Here you can also find more guides for other OSes)
