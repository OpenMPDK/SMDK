#!/bin/bash

readonly TCDIR=$(readlink -f $(dirname $0))
source "$TCDIR/pnm_test_common.sh"

### prerequisite:
### 1. SMDK Kernel is running
### 2. When compiling SMDK Kernel, imdb_resource.ko and imdbcxl.ko must be compiled.

################################# Run Test #################################
# PNM IMDB HW Test
# Description
# - This test verifies that IMDB PNM resource module is inserted/removed normally.
# - Use test files in the PNMLibrary for chekcing actual app operation.
# Pass/Fail
# - Pass : PNM IMDB module is normally inserted/removed.
# - Fail : Failure insert/remove PNM IMDB module.
# - Error : PNMLibrary, SMDK kernel config failure.

#1 Remove inserted modules and check whether dax0.0 exists.
convert_to_devdax
remove_module "imdbcxl"
remove_module "imdb_resource"
check_path_not_exist "/dev/dax0.0"

#2 Insert imdb_resource and imdbcxl modules.
sudo modprobe -v imdbcxl 
convert_to_devdax

#3 Check whether daxdev, imdb_resoruce is created or not.
check_path_exist "/dev/dax0.0"
check_path_exist "/sys/class/pnm/imdb_resource"

#4 Remove inserted modules to finish this test.
remove_module "imdbcxl"
remove_module "imdb_resource"

#5 Fianlly, check created files have been deleted when removing pnm resource modules.
check_path_not_exist "/dev/dax0.0"
check_path_not_exist "/sys/class/pnm/imdb_resource"

echo PASS
############################################################################
