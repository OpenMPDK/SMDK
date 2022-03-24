# prerequisite:
# 1. SMDK Kernel is running
#!/usr/bin/env bash
readonly BASEDIR=$(readlink -f $(dirname $0))/../../..


# cxl policy
CXL_SYSFS_PATH="/sys/kernel/cxl"
MODE_PATH=$CXL_SYSFS_PATH/cxl_mem_mode
POLICYS=("0" "1" "2")
CUR_POL="2"

function print_result(){
        echo --------[Target Policy: $1]--------------------------------------
        echo -n [[CXL_MEMORY_MODE_POLICY]] :
        cat $MODE_PATH
        echo
        echo [[DMESG]]
        dmesg | tail -1
        echo
        echo [[Buddy Info]]
        cat /proc/buddyinfo
        echo
        echo -----------------------------------------------------------------
}

function change_mode_policy(){
        echo $1 > $MODE_PATH
        CUR_POL=$1
}

if [ `whoami` != 'root' ]; then
        echo "You must be root!"
        exit
fi

for i in "${POLICYS[@]}"; do
    echo "===Test Base Policy: $i"============================================
    for j in "${POLICYS[@]}"; do
        change_mode_policy $i
        change_mode_policy $j
        print_result $j
    done
    echo =====================================================================
    echo
done
