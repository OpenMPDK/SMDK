#!/bin/bash

readonly TCDIR=$(readlink -f $(dirname $0))
source "$TCDIR/pnm_test_common.sh"

### prerequisite:
### 1. SMDK Kernel is running
### 2. When compiling SMDK Kernel, sls_resource.ko must be compiled.

################################# Run Test #################################
# PNM DLRM FSIM Test
# Description
# - This test verifies that PNM DLRM resource module is inserted/removed normally.
# - Use test files in the PNMLibrary for chekcing actual app operation.
# Pass/Fail
# - Pass : PNM DLRM module is normally inserted/removed.
# - Fail : Failure insert/remove PNM DLRM module.
# - Error : PNMLibrary, SMDK kernel config failure.

#1 Remove inserted modules and check whether pnm_ctl from PNMLibrary is installed.
check_pnmctl_installed
remove_module "slscxl"
remove_module "sls_resource"
$PNM_CTL destroy-shm --sls 

#2 Insert sls_resource and make shared memory using pnm_ctl
sudo modprobe -v sls_resource
$PNM_CTL setup-shm --sls

#3 Check whether shared memory sls is created or not.
check_path_exist "/dev/shm/sls"

#4 Remove inserted modules to finish this test.
$PNM_CTL destroy-shm --sls
remove_module "sls_resource"

#5 Fianlly, check created files have been deleted when removing pnm resource modules.
check_path_not_exist "/dev/shm/sls"

echo PASS
############################################################################
