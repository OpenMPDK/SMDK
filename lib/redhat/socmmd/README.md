# Samsung Operator for CMM-D (SOCMMD)
![GitHub go.mod Go version](https://img.shields.io/github/go-mod/go-version/wunicorns/gostress) 
[![Go Report Card](https://goreportcard.com/badge/github.com/kubernetes/kubernetes)](https://goreportcard.com/report/github.com/kubernetes/kubernetes)
![GitHub release (latest SemVer)](https://img.shields.io/github/v/release/wunicorns/gostress?sort=semver)   

Samsung Operator for CMM-D provides a way to integrate and utilize CXL Memory Module Device manufactured by Samsung in PasS environment.
This is implemented by utilizing [__technology that utilizes Linux NUMA in CMM-D hierarchy__](https://semiconductor.samsung.com/news-events/tech-blog/utilizing-linux-numa-in-cmm-d-memory-tiering/) and CoreOs and Openshift Operator technology provided by RedHat.
<br>   
> __Compute Express Link(CXL)__: An open standard for high-speed, high-capacity central processing unit CPU-to-device and CPU-to-memory connections designed for high-performance data center computing.   
   
<br>   

## Introduction

The Samsung Operator for CMM-D integrates the server's Local Memory (DRAM) and [__Samsung CMM-D__](https://semiconductor.samsung.com/news-events/tech-blog/worlds-first-cmm-d-technology-leading-the-ai-era/) devices within the Red Hat OpenShift environment. This Golang-based operator enables users to efficiently utilize integrated heterogeneous memory as needed. It provides the ability to select and allocate local memory or expanded Samsung CMM-D resources, ensuring safe and efficient usage under the control of Red Hat's OpenShift Container Platform. Additionally, there is no need to modify CoreOS (kernels) or OpenShift, so Red Hat's support for OpenShift remains unaffected.   

Read more about the Red Hat OpenShift Operator [__here__](https://docs.openshift.com/container-platform/4.15/operators/index.html).
 
<br>   

## Supported Features

The operator supports the following features:

- Recognition of CMM-D installed alongside local memory on the server, with automatic memory integration and expansion.
- Ability to specify the required resources by configuring CPU core and memory NUMA settings.
- Scheduling functionality that ensures new pods are created on server nodes with optimal resource allocation.
- Easy installation through the Red Hat OpenShift Container Platform (OCP) Web Console.
- Integration with OCP event logs for error handling.
- Support for the following Kubernetes-based objects:
	- CRD(Custom Resource Definition)
	- Controller
	- API
	- NodeSelector
	- Resource and Limits
	- Taint/Tolerations   
	- Config Map
	- DaemonSet
   
<br>   	
	
## Support Matrix
The Samsung Operator for CMM-D has been tested and verified on OpenShift and Kubernetes.   
Supported versions include:   

|Kubernetes version|Openshift version| RHEL version|
|-------------------|-------------------|-------------------|
|v1.27.10+c79e5e2|4.14.16|9.2
 
For information on the environment in which the Samsung Operator for CMM-D was tested and verified, please refer to the document [__here__](./document/CXL_requiements.md)

<br>   

## Quick Start Guide
Hardware and software requirements for installing, configuring, and utilizing the Samsung Operator for CMM-D can be found in the document [__here__](./document/Quick_StartGuid.md).   
For instructions on how to use it, please refer to the document [__here__](./document/Quick_Start_Guide.md)  


<br>

## Build and Deploy
To use the Operator in an OLM environment, you must not only build the Operator image but also create a bundle and follow the distribution process provided by the Red Hat Operator community. The steps are as follows.   
<br>

1. Operator Build and Push.
2. Fork Git Repository. (Redhat operator community git)
3. Pull Request.
4. Check Bundle Image Source.
5. Merage.
6. Automatically reflected in the Red Hat OperatorHub Community catalog.   
<br>

For detailed information, please refer to the [__Operator Hub Registration__](./document/Build_Deploy_Guide.md) document.   
<br>


## E2E Test
The End-to-End Test of the Samsung Operator for CMM-D included various use case scenarios involving the Samsung CMM-D equipment:   
- Creation and recovery of pods by manually setting resources (CPU, memory) as needed.
- Automatic generation and allocation of pod resources (CPU, memory) from the target node to the most efficient node.
- Configuration of pod resources to utilize local memory.
  
For detailed information about this test, please refer to the document [__E2E Test Document__](./document/E2E_Test_Guide.md).
