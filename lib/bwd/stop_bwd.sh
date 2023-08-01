#!/usr/bin/env bash

readonly BASEDIR=$(readlink -f $(dirname $0))/../../
source "$BASEDIR/script/common.sh"

if [ `whoami` != 'root' ]; then
	log_error "This script requires root privileges"
	exit 1
fi

if lsmod | grep bwd > /dev/null; then
	log_normal "[rmmod bwd]"
	rmmod bwd
	ret=$?

	if [ $ret = 0 ]; then
		log_normal "[rmmod bwd]..success"
	else
		log_error "[rmmod bwd]..error"
	fi
fi

if pidof bwd > /dev/null; then
	log_normal "[stop bwd]"
	kill -INT $(pidof bwd)
	ret=$?

	if [ $ret = 0 ]; then
		log_normal "[stop bwd]..success"
	else
		log_error "[stop bwd]..error"
	fi
fi
