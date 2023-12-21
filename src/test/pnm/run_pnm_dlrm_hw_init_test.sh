#!/bin/bash

readonly TCDIR=$(readlink -f $(dirname $0))
source "$TCDIR/pnm_test_common.sh"

### prerequisite:
### 1. SMDK Kernel is running
### 2. When compiling SMDK Kernel, sls_resource.ko and slscxl.ko must be compiled.

################################# Run Test #################################
# PNM DLRM HW Test
# Description
# - This test verifies that PNM DLRM resource module is inserted/removed normally.
# - Use test files in the PNMLibrary for chekcing actual app operation.
# Pass/Fail
# - Pass : PNM DLRM module is normally inserted/removed.
# - Fail : Failure insert/remove PNM DLRM module.
# - Error : PNMLibrary, SMDK kernel config failure.

#1 Remove inserted modules and check whether dax0.0 exists.
convert_to_devdax
remove_module "slscxl"
remove_module "sls_resource"
check_path_not_exist "/dev/dax0.0"

#2 Insert sls_resource and slscxl modules.
sudo modprobe -v slscxl 
convert_to_devdax

#3 Check whether daxdev, sls_resoruce are created or not.
check_path_exist "/dev/dax0.0"
check_path_exist "/sys/class/pnm/sls_resource"

#4 Remove inserted modules to finish this test.
remove_module "slscxl"
remove_module "sls_resource"

#5 Fianlly, check created files have been deleted when removing pnm resource modules.
check_path_not_exist "/dev/dax0.0"
check_path_not_exist "/sys/class/pnm/sls_resource"

echo PASS
############################################################################
