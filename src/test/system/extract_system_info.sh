#!/usr/bin/env bash
#
# prerequisite:
#   SMDK Kernel is running
#
# description:
#   the script extracts and checks system information for CXL memory device recognition.
#   the script extracts SRAT tables and checks memory affinity on given address.
#	the script checks the e820 memory map in dmesg.
#   the script checks /proc/iomem.
#   the sciprt checks ExMem zone in /proc/buddyinfo

readonly BASEDIR=$(readlink -f $(dirname $0))/../../../
source "$BASEDIR/script/common.sh"

ACPI_FILENAME=acpidump.out
SRAT_FILENAME=srat.dsl

function check_srat_table(){
	# Extract ACPI Tables
	if [ ! -f $ACPI_FILENAME ]; then
		sudo apt install acpica-tools > apt.log 2>&1
		sudo acpidump -o acpidump.out > acpidump.log 2>&1
		acpixtract -a acpidump.out > acpixtract.log 2>&1
	fi

	if [ ! -f srat.dat ]; then
	   echo -e "\nError: BIOS should publish SRAT to OS.\n"
	   return
	fi

	if [ ! -f $SRAT_FILENAME ]; then
		iasl -d srat.dat > iasl.log 2>&1
	fi

	# Print memory affinity of cxl memory range
	cat srat.dsl | grep -A8 -B6 "$1"
}

usage() { echo "Usage: $0 <start address. e.g. 1080000000>"; exit 0; }
[ $# -eq 0 ] && usage

log_normal "1. SRAT table:"
check_srat_table $1

log_normal "2. e820 memory map:"
dmesg | grep "BIOS-e820"

log_normal "3. /proc/iomem:"
sudo cat /proc/iomem | grep "\<$1\>"

log_normal "4. /proc/buddyinfo:"
cat /proc/buddyinfo
