#!/bin/bash
#
# Prerequisite: Add br0 config in /etc/network/interfaces and reboot.
# 
# $ sudo vi /etc/network/interfaces
# auto lo
# iface lo inet loopback
#
# auto br0
# iface eno2 inet manual
# iface br0 inet static
# 	bridge_ports eno2
# 		address 12.52.13X.XX
# 		broadcast 12.52.133.255
# 		netmask 255.255.254.0
# 		gateway 12.52.132.1
# 	bridge_stp off
# 	bridge_fd 0
# 	bridge_maxwait 0
#
# Or simply,
#
# $ sudo vi /etc/network/interfaces
# auto lo
# iface lo inet loopback
#  
# auto br0
# iface br0 inet dhcp
#         bridge_ports eno2
#         bridge_stp off
#         bridge_fd 0
#         bridge_maxwait 0
#
# It depends on network configuration

function print_usage(){
	echo ""
	echo "Usage:"
	echo " $0 -c <number of VMs>"
	echo ""
}

while getopts "c:" opt; do
	case "$opt" in
		c)
			if [ $OPTARG -lt 0 ] || [ $OPTARG -gt 10 ]; then
				echo "Error: VM count should be 0-10"
				exit 2
			fi
			VMCNT=$OPTARG
			;;
		:)
			print_usage
			exit 2
			;;
	esac
done

if [ -z ${VMCNT} ]; then
	print_usage
	exit 2
fi

num=0
while [ $num -lt $VMCNT ]
do
	TAPNAME=tap${num}
	sudo openvpn --mktun --dev ${TAPNAME}
	sudo ifconfig ${TAPNAME} up
	sudo brctl addif br0 ${TAPNAME}
	num=$(($num+1))
done

