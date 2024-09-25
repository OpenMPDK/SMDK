package node

import (
	"fmt"
	"math"
	"sort"
	"strings"

	corev1 "k8s.io/api/core/v1"
)

const (
	memDiffPctRange = 10 // meminfo 와 daxctl 의 total 크기 오차 허용범위(%)
)

type file struct {
	Filename string `json:"filename"`
	Filepath string `json:"filepath"`
	Exist    bool   `json:"exist"`
	// Size     int    `json:"size"`
}

type numaNode struct {
	NumaNode   int     `json:"numa_node"`
	CPUList    string  `json:"cpu_list"`
	CPUNone    bool    `json:"cpu_none"`
	Movable    bool    `json:"movable"`
	DaxctlMode string  `json:"daxctl_mode"`
	DaxctlSize float64 `json:"daxctl_size"`
	Status     string  `json:"status"` // valid, invalid, unchecked

	Total        float64 `json:"total"`
	Free         float64 `json:"free"`
	Used         float64 `json:"used"`
	MemAvailable float64 `json:"mem_available"`
	SReclaimable float64 `json:"sreclaimable"`
	ActiveFile   float64 `json:"active_file"`
	InactiveFile float64 `json:"inactive_file"`
	Unevictable  float64 `json:"unevictable"`
	Mlocked      float64 `json:"mlocked"`
	DeviceID     string  `json:"device_id"`

	// for pod scheduling
	AvailableMemory float64 // total - reserved
	ReservedMemory  float64 // pod 들의 request(limit) memory
	AvailableErrors []string
}

// memAllocatable
func (r *numaNode) memAllocatable(reqMem float64) bool {
	return r.AvailableMemory >= reqMem
}

// hasCPU true = local memory, false = cmmd memory
func (r *numaNode) hasCPU() bool {
	return r.CPUList != ""
}

// cmmdAvailable
func (r *numaNode) cmmdAvailable() bool {
	result := true

	// 1.daxctl 의 movable=true, /proc/buddyinfo 의 zone=movable
	if !r.Movable {
		r.AvailableErrors = append(r.AvailableErrors, fmt.Sprintf("Movable: %t", r.Movable))
		result = false
	}

	// 2.daxctl 의 mode=system-ram
	if r.DaxctlMode != "system-ram" {
		r.AvailableErrors = append(r.AvailableErrors, fmt.Sprintf("DaxctlMode: %s", r.DaxctlMode))
		result = false
	}

	// 3.meminfo 의 total size 와 daxctl 의 total size 오차가 허용가능한 범위 내
	if r.Total <= 0 {
		r.AvailableErrors = append(r.AvailableErrors, fmt.Sprintf("Invalid total size: %f", r.Total))
		result = false
	}
	diffPct := math.Abs(r.Total-r.DaxctlSize) / r.Total * 100
	if diffPct > float64(memDiffPctRange) {
		r.AvailableErrors = append(r.AvailableErrors, fmt.Sprintf(
			"Memory size differs too big. total: %0.0f, cmmd: %0.0f, diffPct: %0.0f, allowedDiffPct: %d",
			r.Total, r.DaxctlSize, diffPct, memDiffPctRange,
		))
		result = false
	}

	// 4.vendor 정보 확인
	switch r.Status {
	case "valid":
	case "unchecked":
	case "invalid":
		r.AvailableErrors = append(r.AvailableErrors, fmt.Sprintf("Vendor status: %s", r.Status))
		result = false
	default:
		r.AvailableErrors = append(r.AvailableErrors, fmt.Sprintf("Vendor status: unknown(%s)", r.Status))
		result = false
	}

	return result
}

// cmmdAvailableInfo
func (r *numaNode) cmmdAvailableError() string {
	if r.hasCPU() {
		// local memory 사용의 경우 cmmd 상태에러를 판단하지 않음
		return "none(local memory)"
	}
	if len(r.AvailableErrors) == 0 {
		return "none"
	}
	return strings.Join(r.AvailableErrors, "/")
}

// ==============================================================================================
type node struct {
	Hostname                   string
	Hostaddr                   string // ip:port
	RequiredFiles              []file
	NumaNodes                  []numaNode
	LocalMemoryAmount          float64
	LocalReservedMemoryAmount  float64
	LocalAvailableMemoryAmount float64
	Taints                     []corev1.Taint
	Error                      string
}

// readyFiles
func (r *node) readyFiles() bool {
	exists := 0
	for _, f := range r.RequiredFiles {
		if f.Exist {
			exists++
		}
	}
	return exists > 0 && exists == len(r.RequiredFiles)
}

// localMemAllocatable returns whether local memory allocatable or not
func (r *node) localMemAllocatable(reqMemory float64) bool {
	return r.LocalAvailableMemoryAmount >= reqMemory
}

// ==============================================================================================
type nodes []node

func (r *nodes) PrintStatusLog(enable bool, allocateMode string, reqMem float64) {
	for _, node := range *r {
		// file
		fTotal := len(node.RequiredFiles)
		fExists := 0
		for _, file := range node.RequiredFiles {
			if file.Exist {
				fExists++
			}
		}

		// numaNodes
		sort.Slice(node.NumaNodes, func(i, j int) bool {
			return node.NumaNodes[i].NumaNode < node.NumaNodes[j].NumaNode
		})
		numaNodes := []string{}
		for _, numaNode := range node.NumaNodes {
			numaMiB := numaNode.AvailableMemory / math.Pow(1024, 2)
			numaReservedMiB := numaNode.ReservedMemory / math.Pow(1024, 2)
			numaNodes = append(numaNodes,
				fmt.Sprintf("num=%d_hasCPU=%t_mem=%0.0f MiB(%0.3f GiB)_reservedMem=%0.0f MiB(%0.3f GiB)",
					numaNode.NumaNode, numaNode.hasCPU(),
					numaMiB, numaMiB/1024,
					numaReservedMiB, numaReservedMiB/1024,
				),
			)
		}

		// result
		localMiB := node.LocalMemoryAmount / math.Pow(1024, 2)
		localReservedMiB := node.LocalReservedMemoryAmount / math.Pow(1024, 2)
		localAvailableMiB := node.LocalAvailableMemoryAmount / math.Pow(1024, 2)
		logger.Info("Node status",
			"hostname", node.Hostname,
			"file", fmt.Sprintf("%d/%d", fExists, fTotal),
			"localMem", fmt.Sprintf("%0.0f MiB (%0.3f GiB)", localMiB, localMiB/1024),
			"localReservedMem", fmt.Sprintf("%0.0f MiB (%0.3f GiB)", localReservedMiB, localReservedMiB/1024),
			"localAvailableMem", fmt.Sprintf("%0.0f MiB (%0.3f GiB)", localAvailableMiB, localAvailableMiB/1024),
			"numa", strings.Join(numaNodes, " / "),
		)
	}
	reqMemMiB := reqMem / math.Pow(1024, 2)
	reqMemGiB := reqMem / math.Pow(1024, 3)
	logger.Info("Node status requested", "enable", enable, "allocateMode", allocateMode, "requestedMem", fmt.Sprintf("%0.0f MiB (%0.3f GiB)", reqMemMiB, reqMemGiB))

}
