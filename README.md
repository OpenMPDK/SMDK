## Announcing the Release of Scalable Memory Development Kit (SMDK) v2.1

We are pleased to announce the release of SMDK v2.1 to leverage and help CXL ecosystem and industry.
SMDK consists of the following components: plugin, library, cli tool, kernel, testcases, and a guide providing an example workload test.

We previously published pre-release articles introducing SMDK to the industry, as well as demonstrated SMDK functionality at 2024 Dell Tech World, 2024 Red Hat Summit, 2024 MemCon, 2023 Flash Memory Summit, 2023 OCP APAC, 2022 SuperComputing, 2022 HPE Discover, 2021 OCP Global Summit, and so on.
Currently, SMDK has been successfully integrated and tested with many industry partners on a variety of purposes and application workloads, utilizing Samsung’s CXL Memory Expander:

https://news.samsung.com/global/samsung-introduces-industrys-first-open-source-software-solution-for-cxl-memory-platform

https://semiconductor.samsung.com/newsroom/tech-blog/how-samsungs-smdk-simplifies-memory-expander-deployment/

With this public release, we aim to support the ecosystem in adopting software-defined memory and new memory architectures enabled by the CXL interface.
You can find more information about SMDK on our Github.  (https://github.com/OpenMPDK/SMDK)



## CXL Overview

The CXL standard promises to address the increasing demand for memory and bandwidth expansion by enabling host processors (CPU, GPU, etc.) to access DRAM beyond standard DIMM slots on the motherboard, providing new levels of flexibility for different media characteristics (persistence, latency, bandwidth, endurance, etc.) and different media (DDR4, DDR5, LPDDR5, etc., as well as emerging alternative types). This opens up a range of exciting opportunities. While the Samsung CXL Memory Expander is the first example, potential future avenues could include the use of NVDIMM as backup for main memory; asymmetrical sharing of the host’s main memory with CXL-based memory, NVDIMM, and computational storage; and ultimately rack-level CXL-based disaggregation.


## SMDK (Scalable Memory Development Kit)

The SMDK is developed for the Samsung CXL (Compute Express Link) Memory Expander devices to enable a full-stack Software-Defined Memory system.

The SMDK is a collection of software tools and APIs that sit between applications and the hardware, as shown in the link (https://github.com/OpenMPDK/SMDK/wiki/2.-SMDK-Architecture). It supports a diverse range of mixed-use memory scenarios in which the main memory and the Memory Expander can be easily adjusted for priority, usage, bandwidth, and security, or simply used as is without modification to applications on the things they do well by giving them an easy-to-use, portable and scalable SW stack.

The toolkit thus reduces the burden of introducing new memory and allows users to quickly reap the benefits of heterogeneous memory. The SMDK accomplishes this with a four-level approach:

• Memory Interface layer that distinguishes between the onboard bunch of DIMM and CMM(a.k.a CXL Memory Expander) memory, which have different latencies, and optimizes how each interface of memory is utilized.

• Memory Pool Management, which makes the two pools of memory clusters appear as one to applications, and manages memory topography in scalability.

• An Intelligent Tiering Engine, which handles communication between the memory provider and consumer and assigns memory based on the applications’ needs (latency, capacity, bandwidth, etc.).

• A pair of APIs: a Compatible API, enabling end-user to access to the hybrid memories without any changes to their applications, and an Optimization API that can be used to achieve higher levels of performance through application optimization.

• Userspace Cli Tool: a unified tool is provided for users to interact with a CXL device via CXL standard protocol in a human-readable format, as well as manage SMDK features.

• Memory Interfaces: The SMDK provides 3 OS-level memory interfaces for logical use of the CXL device: memory-node, swap, cache

It is worth noting that transparent memory management through the Compatible API is accomplished by inheriting and extending the Linux Process/Virtual Memory Manager (VMM) design. This approach borrows the design and strengths of the Linux kernel while maintaining compatibility with CXL memory.


