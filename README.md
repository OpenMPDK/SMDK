## Announcing the Release of Scalable Memory Development Kit (SMDK) v1.4

We are pleased to announce the release of SMDK v1.4, which consists of a basic SMDK cli tool, allocator library, kernel, and a guide providing an example workload test.

We previously published pre-release articles introducing SMDK to the industry, as well as demonstrated SMDK functionality at 2022 SuperComputing, 2022 HPE Discover, 2021 OCP Global Summit, and so on.
Right now, SMDK has been successfully integrated and tested with many industry partners on a variety of purposes and application workloads using Samsung’s CXL Memory Expander:

https://news.samsung.com/global/samsung-introduces-industrys-first-open-source-software-solution-for-cxl-memory-platform

https://semiconductor.samsung.com/newsroom/tech-blog/how-samsungs-smdk-simplifies-memory-expander-deployment/

With this public release, we aim to support the ecosystem in adopting software-defined memory and new memory architectures enabled by the CXL interface.
You can find more information about SMDK in our github.  (https://github.com/OpenMPDK/SMDK)



## CXL Overview

The CXL standard promises to help address the increasing demand for memory and bandwidth expansion by enabling host processors (CPU, GPU, etc.) to access DRAM beyond standard DIMM slots on the motherboard, providing new levels of flexibility for different media characteristics (persistence, latency, bandwidth, endurance, etc.) and different media (DDR4, DDR5, LPDDR5, etc., as well as emerging alternative types). This opens a range of exciting opportunities. While the Samsung CXL Memory Expander is the first example, potential future avenues could include use of NVDIMM as backup for main memory; asymmetrical sharing of the host’s main memory with CXL-based memory, NVDIMM, and computational storage; and ultimately rack-level CXL-based disaggregation.


## SMDK (Scalable Memory Development Kit)

The SMDK is developed for Samsung CXL(Compute Express Link) Memory Expander to enable full-stack Software-Defined Memory system.

The SMDK is a collection of software tools and APIs that sit between applications and the hardware, as shown in the link(https://github.com/OpenMPDK/SMDK/wiki/2.-SMDK-Architecture). It supports a diverse range of mixed-use memory scenarios in which the main memory and the Memory Expander can be easily adjusted for priority, usage, bandwidth, and security, or simply used as is without modification to applications on the things they do well by giving them an easy-to-use, portable and scalable SW stack.

The toolkit thus reduces the burden of introducing new memory and allows users to quickly reap the benefits of heterogeneous memory. The SMDK accomplishes this with a four-level approach:

• Memory Interface layer that distinguishes between onboard bunch of DIMM memory and Memory Expander (CXL) memory, which have different latencies, and optimizes how each interface of memory is used.

• Memory Pool Management, which makes the two pools of memory cluster appear as one to applications, and manages memory topography in scalability.

• An Intelligent Tiering Engine, which handles communication between the memory provider and consumer and assigns memory based on the applications’ needs. (latency, capacity, bandwidth, etc.)

• A pair of APIs: a Compatible API, which allows end user access to the Memory Expander without any changes to their applications, and an Optimization API that can be used to gain higher levels of performance through optimization of the applications.

• Userspace Cli Tool: an unified tool is provided for users to control and acquire the function and information of a CXL device in a human-readable format, as well as control and manage SMDK features.

• Memory Interfaces : provides 3 OS-level memory interfaces for logical use of CXL device: memory-node, swap, cache

It is worth noting that transparent memory management through the Compatible API is accomplished by inheriting and extending the Linux Process/Virtual Memory Manager (VMM) design, borrowing the design and strengths of the Linux kernel and maintaining its compatibility with CXL memory.



