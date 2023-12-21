#!/bin/bash

readonly TCDIR=$(readlink -f $(dirname $0))
source "$TCDIR/pnm_test_common.sh"

### prerequisite:
### 1. SMDK Kernel is running
### 2. When compiling SMDK Kernel, imdb_resource.ko must be compiled.

################################# Run Test #################################
# PNM IMDB FSIM Test
# Description
# - This test verifies that PNM IMDB resource module is inserted/removed normally.
# - Use test files in the PNMLibrary for chekcing actual app operation.
# Pass/Fail
# - Pass : PNM IMDB module is normally inserted/removed.
# - Fail : Failure insert/remove PNM DLRM module.
# - Error : PNMLibrary, SMDK kernel config failure.

#1 Remove inserted modules and check whether pnm_ctl from PNMLibrary is installed.
check_pnmctl_installed
remove_module "imdbcxl"
remove_module "imdb_resource"
$PNM_CTL destroy-shm --imdb > /dev/null

#2 Insert imdb_resource and make shared memory using pnm_ctl
sudo modprobe -v imdb_resource
$PNM_CTL setup-shm --imdb

#3 Check whether shared memory is created or not.
check_path_exist "/dev/shm/imdb-control"
check_path_exist "/dev/shm/imdb-data"

#4 Remove inserted modules to finish this test.
$PNM_CTL destroy-shm --imdb
remove_module "imdb_resource"

#5 Fianlly, check created files have been deleted when removing pnm resource modules.
check_path_not_exist "/dev/shm/imdb-control"
check_path_not_exist "/dev/shm/imdb-data"

echo PASS
############################################################################
