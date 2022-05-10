# prerequisite:
# 1. SMDK Kernel is running
#!/usr/bin/env bash
readonly BASEDIR=$(readlink -f $(dirname $0))/../../..


# cxl policy
CXL_SYSFS_PATH="/sys/kernel/cxl"
POLICY_PATH=$CXL_SYSFS_PATH/cxl_group_policy
MODE_PATH=$CXL_SYSFS_PATH/cxl_mem_mode

function print_result(){
        echo
		echo -n [[CXL_GROUP_POLICY]] : 
        cat $POLICY_PATH 
        echo [[Buddy Info]]
        cat /proc/buddyinfo
        echo -----------------------------------------------------------------
}

function change_group_policy(){    
        echo $1 > $POLICY_PATH
        CUR_POL=$1
}

if [ `whoami` != 'root' ]; then
        echo "You must be root!"
        exit
fi

echo Zone Test
echo zone > $POLICY_PATH
print_result
echo noop > $POLICY_PATH & sleep 1s; ./mmap_cxl
print_result

echo Node Test
echo node > $POLICY_PATH
print_result
echo zone > $POLICY_PATH & sleep 1s; ./mmap_cxl
print_result

echo Noop Test
echo noop > $POLICY_PATH
print_result
echo zone > $POLICY_PATH & sleep 1s; ./mmap_cxl
print_result


echo Mem Test
print_result
echo 0 > $MODE_PATH & sleep 1s; ./mmap_cxl
print_result

 
