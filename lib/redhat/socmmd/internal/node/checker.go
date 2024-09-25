package node

import (
	"context"
	"encoding/json"
	"fmt"
	"math"
	"strconv"

	cmmdv1 "samsung/socmmd/api/v1"
	"samsung/socmmd/internal/consts"
	"samsung/socmmd/internal/packageinfo"
	"samsung/socmmd/internal/patcher"
	"samsung/socmmd/internal/utils"

	corev1 "k8s.io/api/core/v1"
	"k8s.io/apimachinery/pkg/types"
	ctrl "sigs.k8s.io/controller-runtime"
	"sigs.k8s.io/controller-runtime/pkg/client"
)

var (
	logger = ctrl.Log.WithName("Node-Checker")

	metricURL = "/metrics"
)

type Allocatable struct {
	NodeName string
	MemNuma  string
	CPUList  string
}

type nodeResponse struct {
	Files     []file     `json:"files"`
	NumaNodes []numaNode `json:"nodes"`
	Error     string     `json:"error"`
}

type checker struct {
	client client.Client
}

func NewChecker(c *client.Client) *checker {
	r := &checker{client: *c}
	return r
}

// NodeCheck returns nodes info and allocatable info
/*
	1.Enable = true, AllocateMode = auto
		- file 존재 여부 확인 - hook, daxctl
	  - cmmd numaNode 여부 확인 --> zero cpu
		- cmmd 상태 조건 확인 --> cmmd avaiable
			- daxctl 의 movable=true, /proc/buddyinfo 의 zone=movable
			- daxctl 의 mode=system-ram
			- meminfo 의 total size 와 daxctl 의 total size 오차가 허용가능한 범위 내
		- cmmd 가용 메모리 확인 --> mem allocatable
	2.Enable = true, AllocateMode = manual
		- node, memNuma, cpuList(auto 인 경우 별도 지정안함)
	3.Enable = false
		- local memory 크기 확인
	* 최적 node/numaNode 선정 --> 가용 메모리 크기 기준
*/
func (r *checker) NodeCheck(spec cmmdv1.CMMDSpec, reqMem float64) (*Allocatable, error) {
	var nodes *nodes
	var alloc *Allocatable
	var err error

	// get target node label
	podSpec, err := utils.GetPodSpecFromPayload(spec.Payload)
	if err != nil {
		logger.Error(err, "Failed to get pod spec from payload", "payload", spec.Payload)
		return nil, err
	}

	// get node list
	nodes, err = r.getNodes(podSpec.NodeSelector)
	if err != nil {
		logger.Error(err, "Failed to check nodes")
		return nil, err
	}
	nodes.PrintStatusLog(spec.Enable, spec.AllocateMode, reqMem)

	// get allocatable node/numaNode
	switch {
	case !spec.Enable:
		nodes = r.tolerationFilteredNodes(nodes, podSpec)
		alloc, err = r.localAllocatable(nodes, reqMem)

	case spec.Enable && spec.AllocateMode == consts.AllocateModeAuto:
		nodes = r.tolerationFilteredNodes(nodes, podSpec)
		alloc, err = r.cmmdAllocatableForAuto(nodes, reqMem)

	case spec.Enable && spec.AllocateMode == consts.AllocateModeManual:
		alloc, err = r.cmmdAllocatableForManual(nodes, reqMem, spec)

	default:
		err = fmt.Errorf("not supported options error")
	}

	return alloc, err
}

// tolerationFilteredNodes
func (r *checker) tolerationFilteredNodes(nodeList *nodes, podSpec *corev1.PodSpec) *nodes {
	filteredNodes := nodes{}
	for _, node := range *nodeList {
		permittedCnt := 0

		for _, taint := range node.Taints {
			for _, tol := range podSpec.Tolerations {
				if taint.Key != tol.Key || taint.Effect != tol.Effect {
					continue
				}
				switch tol.Operator {
				case corev1.TolerationOpEqual:
					if taint.Value == tol.Value {
						permittedCnt++
					}
				case corev1.TolerationOpExists:
					permittedCnt++
				}
			}
		}

		if len(node.Taints) == permittedCnt {
			filteredNodes = append(filteredNodes, node)
		}
	}
	return &filteredNodes
}

// localAllocatable returns whether local memory allocatable or not
func (r *checker) localAllocatable(nodes *nodes, reqMem float64) (*Allocatable, error) {
	bestNode := ""
	bestAvailableMemory := float64(0)

	for _, node := range *nodes {
		logger.Info("Allocatable info", "hostname", node.Hostname,
			"memLocalTotalGiB", fmt.Sprintf("%0.3f", node.LocalMemoryAmount/math.Pow(1024, 3)),
			"memLocalReservedGiB", fmt.Sprintf("%0.3f", node.LocalReservedMemoryAmount/math.Pow(1024, 3)),
			"memLocalAvailableGiB", fmt.Sprintf("%0.3f", node.LocalAvailableMemoryAmount/math.Pow(1024, 3)),
			"memLocalRequestedGiB", fmt.Sprintf("%0.3f", reqMem/math.Pow(1024, 3)),
		)

		if !node.localMemAllocatable(reqMem) {
			continue
		}

		if node.LocalAvailableMemoryAmount > bestAvailableMemory {
			logger.Info("Most allocatable node changed", "old-hostname", bestNode, "new-hostname", node.Hostname)
			bestNode = node.Hostname
			bestAvailableMemory = node.LocalAvailableMemoryAmount
		}
	}

	if bestNode == "" {
		return nil, fmt.Errorf("not found allocatable node error: "+
			"ensure that free space of memory or total request memory size (request memory x replicas = %0.3fGiB)", reqMem/math.Pow(1024, 3))
	}

	alloc := &Allocatable{
		NodeName: bestNode,
		MemNuma:  "",
		CPUList:  "",
	}

	return alloc, nil
}

// cmmdAllocatableForAuto returns allocatable cmmd numaNode
func (r *checker) cmmdAllocatableForAuto(nodes *nodes, reqMem float64) (*Allocatable, error) {
	bestNode := ""
	bestNumaNode := numaNode{
		NumaNode:        -1,
		AvailableMemory: 0,
	}

	for _, node := range *nodes {
		if !node.readyFiles() {
			logger.Info("Allocatable info", "hostname", node.Hostname, "fail", "files not ready")
			continue
		}
		for _, numaNode := range node.NumaNodes {
			isAllocatable := false
			if !numaNode.hasCPU() && numaNode.cmmdAvailable() && numaNode.memAllocatable(reqMem) {
				isAllocatable = true
			}

			logger.Info("Allocatable info", "hostname", node.Hostname, "numaNode", numaNode.NumaNode,
				"allocatable", isAllocatable,
				"hasCPU", numaNode.hasCPU(),
				"memAllocatable", numaNode.memAllocatable(reqMem),
				"cmmdAllocatable", numaNode.cmmdAvailable(),
				"cmmdAllocatableError", numaNode.cmmdAvailableError(),
				"memTotalGiB", fmt.Sprintf("%0.3f", numaNode.Total/math.Pow(1024, 3)),
				"memReservedGiB", fmt.Sprintf("%0.3f", numaNode.ReservedMemory/math.Pow(1024, 3)),
				"memAvailableGiB", fmt.Sprintf("%0.3f", numaNode.AvailableMemory/math.Pow(1024, 3)),
				"memRequestedGiB", fmt.Sprintf("%0.3f", reqMem/math.Pow(1024, 3)),
			)

			if !isAllocatable {
				continue
			}

			if numaNode.AvailableMemory > bestNumaNode.AvailableMemory {
				logger.Info("Most allocatable node changed", "old-hostname", bestNode, "old-numanode", bestNumaNode, "new-hostname", node.Hostname, "new-numanode", numaNode)
				bestNode = node.Hostname
				bestNumaNode = numaNode
			}
		}
	}

	if bestNode == "" {
		return nil, fmt.Errorf("not found allocatable node error: "+
			"ensure that free space of memory or total request memory size (request memory x replicas = %0.3fGiB) or CMMD device status", reqMem/math.Pow(1024, 3))
	}

	alloc := &Allocatable{
		NodeName: bestNode,
		MemNuma:  strconv.Itoa(bestNumaNode.NumaNode),
		CPUList:  bestNumaNode.CPUList,
	}

	return alloc, nil
}

// cmmdAllocatableForManual returns whether allocatable or not
func (r *checker) cmmdAllocatableForManual(nodes *nodes, reqMem float64, spec cmmdv1.CMMDSpec) (*Allocatable, error) {
	alloc := &Allocatable{
		NodeName: spec.Allocate.NodeName,
		MemNuma:  spec.Allocate.Memory,
		CPUList:  "",
	}
	if spec.Allocate.CPU != consts.AllocateModeAuto {
		alloc.CPUList = spec.Allocate.CPU
	}

	// check available
	for _, node := range *nodes {
		if node.Hostname != spec.Allocate.NodeName {
			continue
		}

		if !node.readyFiles() {
			logger.Info("Allocatable info", "hostname", node.Hostname, "fail", "files not ready")
			return nil, fmt.Errorf("not found required files error")
		}

		for _, numaNode := range node.NumaNodes {
			if strconv.Itoa(numaNode.NumaNode) != spec.Allocate.Memory {
				continue
			}

			isAllocatable := false
			if !numaNode.hasCPU() && numaNode.cmmdAvailable() && numaNode.memAllocatable(reqMem) {
				isAllocatable = true
			}

			logger.Info("Allocatable info", "hostname", node.Hostname, "numaNode", numaNode.NumaNode,
				"allocatable", isAllocatable,
				"hasCPU", numaNode.hasCPU(),
				"memAllocatable", numaNode.memAllocatable(reqMem),
				"cmmdAllocatable", numaNode.cmmdAvailable(),
				"cmmdAllocatableError", numaNode.cmmdAvailableError(),
				"memTotalGiB", fmt.Sprintf("%0.3f", numaNode.Total/math.Pow(1024, 3)),
				"memReservedGiB", fmt.Sprintf("%0.3f", numaNode.ReservedMemory/math.Pow(1024, 3)),
				"memAvailableGiB", fmt.Sprintf("%0.3f", numaNode.AvailableMemory/math.Pow(1024, 3)),
				"memRequestedGiB", fmt.Sprintf("%0.3f", reqMem/math.Pow(1024, 3)),
			)

			if !isAllocatable {
				return nil, fmt.Errorf("not allocatable node error: "+
					"ensure that free space of memory or total request memory size (request memory x replicas = %0.3fGiB)", reqMem/math.Pow(1024, 3))
			}

			return alloc, nil
		}
		logger.Info("Allocatable info", "hostname", spec.Allocate.NodeName, "fail", "not found numanode")
		return nil, fmt.Errorf("not found numanode error")
	}
	logger.Info("Allocatable info", "hostname", spec.Allocate.NodeName, "fail", "not found node")
	return nil, fmt.Errorf("not found node error")
}

// getNodes
func (r *checker) getNodes(nodeSelector map[string]string) (*nodes, error) {
	nodeCheck := nodes{}

	// target node list
	nodes := []node{}
	eps, err := r.getNodeEndpoints(nodeSelector)
	if err != nil {
		return nil, err
	}
	nodes = append(nodes, *eps...)
	logger.Info("Endpoints", "data", nodes)

	// get reserved mem
	reservedMemMap, err := r.getReservedMem()
	logger.Info("ReservedMap", "data", reservedMemMap)
	if err != nil {
		return nil, err
	}

	// node info
	for _, node := range nodes {
		// remote call
		res, err := r.remoteCall(node.Hostname, node.Hostaddr)

		node.NumaNodes = res.NumaNodes
		node.RequiredFiles = res.Files
		if res.Error != "" {
			node.Error = fmt.Sprintf("remote node error : %s", res.Error)
		} else if err != nil {
			node.Error = err.Error()
		}

		// set reserved memory per numanode
		for i, numanode := range node.NumaNodes {
			reservedMem := float64(0)

			numaNum := strconv.Itoa(numanode.NumaNode)
			if nodeMemMap, ok := reservedMemMap[node.Hostname]; ok {
				if mem, ok := nodeMemMap[numaNum]; ok {
					reservedMem += mem
				}
			}

			// numanode memory
			node.NumaNodes[i].ReservedMemory = reservedMem
			node.NumaNodes[i].AvailableMemory = numanode.Total - reservedMem

			// local memory 인 경우 node 의 local memory 에 합산
			if numanode.hasCPU() {
				node.LocalMemoryAmount += numanode.Total
				node.LocalReservedMemoryAmount += reservedMem
			}
		}

		// local memory 의 경우 numanode 미지정인 경우도 있음(auto)
		if nodeMemMap, ok := reservedMemMap[node.Hostname]; ok {
			if reservedMem, ok := nodeMemMap[""]; ok {
				node.LocalReservedMemoryAmount += reservedMem
			}
		}

		node.LocalAvailableMemoryAmount = node.LocalMemoryAmount - node.LocalReservedMemoryAmount

		nodeCheck = append(nodeCheck, node)
	}

	return &nodeCheck, nil
}

// getReservedMem
func (r *checker) getReservedMem() (map[string]map[string]float64, error) {
	// pod 조회
	podList := &corev1.PodList{}
	labels := map[string]string{consts.LabelOperatorKey: consts.LabelOperatorVal}
	listOpts := []client.ListOption{client.MatchingLabels(labels)}
	err := r.client.List(context.TODO(), podList, listOpts...)
	if err != nil {
		return nil, fmt.Errorf("get pod list error: %w", err)
	}

	// server-numanode 별 pod 메모리 집계
	podMap := map[string]map[string]float64{}
	for _, pod := range podList.Items {
		nodeName := pod.Spec.NodeName
		numaNum := pod.Annotations[consts.AnnoHookMemNuma]
		reservedMem := utils.GetAmountMemory(consts.ObjectKindPod, &pod)

		if _, ok := podMap[nodeName]; !ok {
			podMap[nodeName] = map[string]float64{}
		}
		if _, ok := podMap[nodeName][numaNum]; !ok {
			podMap[nodeName][numaNum] = 0
		}

		podMap[nodeName][numaNum] += float64(reservedMem.Value())
	}

	return podMap, nil
}

// getNodeEndpoints
func (r *checker) getNodeEndpoints(nodeSelector map[string]string) (*[]node, error) {
	nodes := []node{}

	// nodes for ip:hostname
	listOpts := []client.ListOption{client.MatchingLabels(nodeSelector)}
	nodeList := &corev1.NodeList{}
	if err := r.client.List(context.TODO(), nodeList, listOpts...); err != nil {
		return nil, fmt.Errorf("get node list error: %w", err)
	}

	if len(nodeList.Items) == 0 {
		return nil, fmt.Errorf("get node list error: empty list: nodeSelector: %v", nodeSelector)
	}

	// set temp node info
	type nodeInfo struct {
		hostname string
		taints   []corev1.Taint
	}
	nodeInfoMap := map[string]nodeInfo{}
	for _, node := range nodeList.Items {
		// ip, hostname
		var hostname, ip string
		for _, addrs := range node.Status.Addresses {
			switch addrs.Type {
			case "Hostname":
				hostname = addrs.Address
			case "InternalIP":
				ip = addrs.Address
			}
		}
		nodeInfoMap[ip] = nodeInfo{
			hostname: hostname,
			taints:   node.Spec.Taints,
		}
	}

	// endpoints for ip:port
	epKey := types.NamespacedName{Namespace: packageinfo.GetNamespace(), Name: patcher.GetDaemonsetServiceName()}
	logger.Info("Search endpoint", "searchKey", epKey)

	ep := &corev1.Endpoints{}
	if err := r.client.Get(context.TODO(), epKey, ep); err != nil {
		return nil, fmt.Errorf("get endpoints error: %w", err)
	}

	for _, sub := range ep.Subsets {
		for _, addr := range sub.Addresses {
			for _, port := range sub.Ports {
				if nodeInfo, ok := nodeInfoMap[addr.IP]; ok {
					node := node{
						Hostname: nodeInfo.hostname,
						Hostaddr: fmt.Sprintf("%s:%d", addr.IP, port.Port),
						Taints:   nodeInfo.taints,
					}
					nodes = append(nodes, node)
				}
			}
		}
	}

	return &nodes, nil
}

// remoteCall
func (r *checker) remoteCall(hostname, addr string) (*nodeResponse, error) {
	nodeRes := &nodeResponse{}

	urlPath := fmt.Sprintf("http://%s%s", addr, metricURL)

	data, err := Get(urlPath)

	if err != nil {
		return nodeRes, fmt.Errorf("unmarshal node response error: %w", err)
	}

	err = json.Unmarshal(data, nodeRes)
	if err != nil {
		return nodeRes, fmt.Errorf("unmarshal node response error: %w", err)
	}

	logger.Info("Node response", "hostname", hostname, "destination", urlPath, "data", nodeRes)

	return nodeRes, nil
}
