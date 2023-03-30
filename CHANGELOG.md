# Changelog

## v1.4: CXL-Cache / Userspace CLI Tool (update)

### This update reflects the voices of Industry partners who cooperate with us.

**1. CXL Cache**
- In addition to the usecase that uses CXL device as **System RAM** and **Swap** interfaces, SMDK v1.4 allows another usecase that uses CXL device as **OS-Level Cache** interface. 
- CXL Cache is the 2nd-level page cache with pluggable and page-granularity attributes that stores clean file-backed pages. 
Upon CXL Cache, a file-backed page is traversed in following memory order - pagecache(near), cxlcache(far), disk(farthest).


<br>

**2. CXL-CLI**
- CXL Spec commands added
> identify, get-health-info, set-alert-config, get-alert-config, get-firmware-info, transfer-firmware, activate-firmware
- CXL Cache control commands added
> enable-cxlcache, disable-cxlcache, flush-cxlcache, check-cxlcache

<br>

**3. Documentation**
- Update user guide documents
> SMDK Architecture: CXL Cache - https://github.com/OpenMPDK/SMDK/wiki/2.-SMDK-Architecture <br>
> Kernel Guide: CXL Cache usage - https://github.com/OpenMPDK/SMDK/wiki/4.-Kernel <br>
> Plugin Guide: New CXL spec commands and CXL Cache control commands - https://github.com/OpenMPDK/SMDK/wiki/5.-Plugin

<br>


## v1.3: CXL-Swap / CXL Composability / Userspace CLI Tool (update)

### This update reflects the voices of Industry partners who cooperate with us.

**1. CXL Swap**
- In addition to the usecase that uses CXL device as **System RAM** interface,
SMDK v1.3 allows another usecase that uses CXL device as **Swap interface**.  CXL Swap implements Linux *frontswap* and is delivered as kernel module likewise *zSwap*.
- However, it does not perform (de)compression while page swapping, thus, leads to CPU saving and latency QoS on swap handling. We believe it is more close to the philosophy of CXL memory expansion.

<br>

**2. CXL Composability**
- Composability is also a primary matter of CXL philosophy. By adopting previous Linux patches and algorithms, we enhanced compatibility and coverage of Linux VMM belows to better support of CXL composability.
- Page migration - manage CXL DRAM as *movable* page attribute
- Memory node on/offline -  allow memory-node on/offline function for CXL DRAM
- Fragmentation avoidance - apply the algorithm for avoiding DRAM fragmentation issue to CXL DRAM

<br>

**3. CXL-CLI**
- CXL Swap control commands
> enable_cxlswap, disable_cxlswap, flush_cxlswap, check_cxlswap
- CXL Device performance reporter
> get-latency-matrix

<br>

**4. Documentation**
- Update and Refactoring user guide documents
> SMDK Architecture - https://github.com/OpenMPDK/SMDK/wiki/2.-SMDK-Architecture <br>
> Installation Guide - https://github.com/OpenMPDK/SMDK/wiki/3.-Installation <br>
> Kernel Guide - https://github.com/OpenMPDK/SMDK/wiki/4.-Kernel <br>
> Plugin Guide - https://github.com/OpenMPDK/SMDK/wiki/5.-Plugin

<br>


## v1.2: N-way Grouping / Userspace CLI Tool / Application Compatibility

### This update reflects the voices of Industry partners who cooperate with us.

**1. N-way grouping (a.k.a memory partition)**
- **This is geared for those who want to assemble multiple CXL devices as they wish**
    - Related Usecase : Memory Interleaving, Isolation, Virtualization
- CXL-CLI - group / node management
- SMDK Allocator - updated compatible / optimization library
- SMDK Kernel - primitive interfaces to provide online and static CXL device information, and control

<br>

**2. CXL-CLI**
- Grouping commands
> N-way group : group-zone / group-node / group-noop / group-dax / group-add / group-remove <br>
> Listing group : group-list
- CXL Spec commands
> Poison : inject-poison / get-poison / clear-poison <br>
> Timestamp : set-timestamp / get-timestamp <br>
> Event : get-event-record / clear-event-record
- Basic CXL-CLI commands
> SMDK CXL-CLI is an expansion of Intel CXL-CLI

<br>

**3. Application Compatibility**
- A multi-node traversal application is now able to run normally. e.g.) MLC

<br>

**4. Miscellaneous**
 - Error/Exception handling on real-world testbeds
 - More testcases

<br>

**5. Issue**
 - fix issue : kernel build failure when CONFIG_LOCKDEP=y or CONFIG_EXMEM=n (v1.2.1, thanks to Ravi Shankar)
 - functionality coverage : Even though a version of BIOS and/or CXL device does not provide DVSEC ID info, the patch allows listing CXL device information and node grouping. (v1.2.2, thanks to Wu Chanco)

<br>

**Documentation**
- SMDK Architecture - https://github.com/OpenMPDK/SMDK/wiki/2.-SMDK-Architecture
- User Guide - https://github.com/OpenMPDK/SMDK/wiki/3.-User-Guide
- Test and Tools - https://github.com/OpenMPDK/SMDK/wiki/4.-Test-and-Tools

<br>


## v1.1.0: Coverage/Reliability on Compatible/Optimization API + Memory Partition

This update reinforces coverage and reliability of v1.0.0 compatible/optimization API and memory partition features.

### SMDK Allocator
 - #### Compatible API 
 1. BW Aggregation / Isolation
 2. Easy-configuration 
 - #### Optimization API
 1. Python Binding
 2. Node Specific Allocation/deallocation
 3. Statistic Reporting

<br>

### SMDK Kernel
1. Co-existence with DAX interface
2. CXL Memory Registration using SRAT/CEDT/DVSEC
3. Multi-socket Support
4. Update version 5.17-rc5 -> 5.18-rc3

<br>

### Miscellaneous 
1. Error/Exception Handling
2. Testcases

<br>

### Documentation (https://github.com/OpenMPDK/SMDK/wiki)
- User Guide
- Test and Tools


<br>


## v1.0.0: Compatible/Optimization API + Memory Partition

The initial OSS release of the Scalable Memory Development Kit (SMDK).

### Software (https://github.com/OpenMPDK/SMDK.git)
- SMDK Plugin (allocator library, numactl extension)
- SMDK Kernel (memory partition)
- Application (IMDB, ML/AI)
- BM Tool (stream, mlc)
- Container (Application/BM Tool container)
- TC (unittests)

<br>

### Documentation (https://github.com/OpenMPDK/SMDK/wiki)
- Design Principle and Objective
- SMDK Architecture
- User Guide
- Test and Tools
- Experiment Results

<br>


