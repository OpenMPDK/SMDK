package consts

var (
	OperatorFinalizers = []string{"cmmd.samsung.com/cleanup"}
)

const (

	// manager name
	Manager   = "socmmd"
	NodeLabel = "cmmd-node-label"

	ConfigMapName           = "cmmd-config"
	ConfigMapTolerationsKey = "CMMD_ND_TOLERATIONS"

	ReallocateReasonNodeNotFound = "nodeNotFound"
	ReallocateReasonNodeNotReady = "nodeNotReady"
	ReallocateReasonTolerations  = "tolerationsNotStaisfy"
	ReallocateReasonCMMDFail     = "cmmdFail"

	WarmUpTrue      = "true"
	WarmUpTypeStart = "start"
	WarmUpTypeDone  = "done"

	// object kind
	ObjectKindMC                 = "MachineConfig"
	ObjectKindPod                = "Pod"
	ObjectKindDeployment         = "Deployment"
	ObjectKindReplicaset         = "Replicaset"
	ObjectKindDaemonset          = "DaemonSet"
	ObjectKindService            = "Service"
	ObjectKindServiceAccount     = "ServiceAccount"
	ObjectKindConfigMap          = "ConfigMap"
	ObjectKindClusterRole        = "ClusterRole"
	ObjectKindClusterRoleBinding = "ClusterRoleBinding"
	ObjectKindConsole            = "Console"
	ObjectKindConsolePlugin      = "ConsolePlugin"

	// labels for memory available
	LabelOperatorKey     = "socmmd"
	LabelOperatorVal     = "v1"
	LabelAllocateNodeKey = "cmmd-allocate-node"
	LabelMemTypeKey      = "socmmd-type"
	LabelMemTypeValLocal = "local"
	LabelMemTypeValCMMD  = "cmmd"

	// allocate options
	AllocateModeAuto   = "auto" // allocateMode, allocate.CPU
	AllocateModeManual = "manual"

	// hook
	AnnoHookEnable  = "cmmd-enable"
	AnnoHookMemNuma = "cmmd-numa"
	AnnoHookCPUNuma = "cmmd-cpus"

	// hook for warm up
	AnnoHookWarmUp     = "warmup"      // true
	AnnoHookWarmUpType = "warmup-type" // start or done
	AnnoHookWarmUpHost = "warmup-host" // hostname
	AnnoHookWarmUpCID  = "warmup-cid"  // container ID

	// scheduling
	AnnoOwnerCR    = "cmmd.samsung.com/owner"
	AnnoReallocate = "cmmd.samsung.com/reallocate"

	// annotaions for compare CR
	AnnoSpecEnable       = "cmmd.samsung.com/enable"
	AnnoSpecAllocateMode = "cmmd.samsung.com/allocateMode"
	AnnoSpecAllocateNode = "cmmd.samsung.com/node"
	AnnoSpecAllocateMem  = "cmmd.samsung.com/memory"
	AnnoSpecAllocateCPU  = "cmmd.samsung.com/cpu"

	// predicate
	AnnoPredicatesPhase   = "cmmd.samsung.com/predicate"
	PredicatesPhaseCreate = "create"
	PredicatesPhaseUpdate = "update"
	PredicatesPhaseDelete = "delete"

	// event reasons
	EventReasonCreated      = "Created"
	EventReasonFailedCR     = "FailedCR"
	EventReasonFailedWatch  = "FailedWatch"
	EventReasonFailedWarmUp = "FailedWarmUp"
	EventReasonFailedCMMD   = "FailedCMMD"
	EventReasonFailed       = "Failed"
	EventReasonBackOff      = "BackOff"
	EventReasonModified     = "Modified"
	EventReasonConfigured   = "Configured"
	EventReasonScheduled    = "Scheduled"
	EventReasonRevision     = "Revision"

	// env with downapi
	EnvOperProps     = "OPER_PROPS"
	EnvOperNamespace = "OPER_NAMESPACE"

	// for local test
	EnvOperRunMode     = "OPER_RUN_MODE"
	EnvOperRunModeTest = "test"
)
