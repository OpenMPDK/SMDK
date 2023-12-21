#!/usr/bin/env bash

readonly BASEDIR=$(readlink -f $(dirname $0))/../../
source "$BASEDIR/script/common.sh"

if [ `whoami` != 'root' ]; then
	log_error "This script requires root privileges"
	exit 1
fi

if pidof tierd > /dev/null; then
	log_normal "[stop tierd]"
	kill -INT $(pidof tierd)
	ret=$?

	if [ $ret = 0 ]; then
		log_normal "[stop tierd]..success"
	else
		log_error "[stop tierd]..error"
	fi
fi
