/*
Copyright 2024.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
*/

package controllers

import (
	"context"
	"fmt"
	"strconv"
	"strings"

	"k8s.io/apimachinery/pkg/api/errors"
	metav1 "k8s.io/apimachinery/pkg/apis/meta/v1"
	"k8s.io/apimachinery/pkg/runtime"
	"k8s.io/apimachinery/pkg/types"
	"k8s.io/client-go/tools/record"
	ctrl "sigs.k8s.io/controller-runtime"
	"sigs.k8s.io/controller-runtime/pkg/builder"
	"sigs.k8s.io/controller-runtime/pkg/handler"

	"sigs.k8s.io/controller-runtime/pkg/client"
	"sigs.k8s.io/controller-runtime/pkg/event"
	"sigs.k8s.io/controller-runtime/pkg/predicate"

	cmmdv1 "samsung/socmmd/api/v1"
	"samsung/socmmd/internal/consts"
	"samsung/socmmd/internal/node"
	"samsung/socmmd/internal/utils"

	"github.com/go-logr/logr"
	appsv1 "k8s.io/api/apps/v1"
	corev1 "k8s.io/api/core/v1"
)

var (
	logger = ctrl.Log.WithName("CMMD-Controller")
)

// CMMDReconciler reconciles a CMMD object
type CMMDReconciler struct {
	client.Client
	Scheme   *runtime.Scheme
	Recorder record.EventRecorder
}

//+kubebuilder:rbac:groups=cmmd.samsung.com,resources=cmmds;labels,verbs=get;list;watch;create;update;patch;delete
//+kubebuilder:rbac:groups=cmmd.samsung.com,resources=cmmds/status;labels/status,verbs=get;update;patch
//+kubebuilder:rbac:groups=cmmd.samsung.com,resources=cmmds/finalizers;labels/finalizers,verbs=update
//+kubebuilder:rbac:groups="",resources=pods,verbs=get;list;create;update;delete;watch
//+kubebuilder:rbac:groups="",resources=nodes,verbs=get;list;watch
//+kubebuilder:rbac:groups="",resources=services,verbs=get;list;create;delete;update;watch
//+kubebuilder:rbac:groups="",resources=serviceaccounts,verbs=get;list;create;delete;update;watch
//+kubebuilder:rbac:groups="",resources=endpoints,verbs=get;list;create;delete;watch
//+kubebuilder:rbac:groups="",resources=configmaps,verbs=get;list;create;delete;update;watch
//+kubebuilder:rbac:groups="";events.k8s.io,resources=events,verbs=create;patch;watch
//+kubebuilder:rbac:groups=apps,resources=daemonsets,verbs=get;list;create;update;delete;watch
//+kubebuilder:rbac:groups=apps,resources=deployments,verbs=get;list;create;update;delete;watch
//+kubebuilder:rbac:groups=apps,resources=replicasets,verbs=get;list;create;update;delete;watch
//+kubebuilder:rbac:groups=rbac.authorization.k8s.io,resources=clusterroles,verbs=*
//+kubebuilder:rbac:groups=rbac.authorization.k8s.io,resources=clusterrolebindings,verbs=*
//+kubebuilder:rbac:groups="machineconfiguration.openshift.io",resources=machineconfigs,verbs=get;list;create;update;patch;delete;watch
//+kubebuilder:rbac:groups="operators.coreos.com",resources=clusterserviceversions,verbs=get;list;watch
//+kubebuilder:rbac:groups="operator.openshift.io",resources=consoles,verbs=get;list;create;update;delete;watch
//+kubebuilder:rbac:groups="console.openshift.io",resources=consoleplugins,verbs=get;list;create;update;delete;watch
//+kubebuilder:rbac:groups="security.openshift.io",resources=securitycontextconstraints,verbs=use

func (r *CMMDReconciler) Reconcile(ctx context.Context, req ctrl.Request) (ctrl.Result, error) {
	logger.Info("====================================================================================================")
	reqLogger := logger.WithValues("Request.Namespace", req.Namespace, "Request.Name", req.Name)
	reqLogger.Info("Reconciling start")

	// ---------------------------------------------------------------------------------------------
	// 1. 제출 CR 조회
	cmmd := &cmmdv1.CMMD{}
	err := r.Get(ctx, req.NamespacedName, cmmd)
	if err != nil {
		if errors.IsNotFound(err) {
			reqLogger.Info("CMMD resource not found. Ignoring since object must be deleted")
			return ctrl.Result{}, nil
		}
		reqLogger.Error(err, "Failed to get CMMD")
		r.Recorder.Eventf(cmmd, corev1.EventTypeWarning, consts.EventReasonFailedCR, "Failed to get CMMD: %s", err)
		return ctrl.Result{}, err
	}

	// ---------------------------------------------------------------------------------------------
	// 2. Object Spec 확인
	foundObj, objKind, err := utils.ConvertPayloadObject(cmmd.Spec.Payload)
	if err != nil {
		reqLogger.Error(err, "Failed to get object template")
		r.Recorder.Eventf(cmmd, corev1.EventTypeWarning, consts.EventReasonFailedCR, "Failed to get object from payload: %s", err)
		return ctrl.Result{}, err
	}

	// check object (create)
	err = r.Get(ctx, types.NamespacedName{Namespace: req.Namespace, Name: req.Name}, foundObj)
	if err != nil {
		if errors.IsNotFound(err) {
			// create
			reqLogger.Info("Creating a new object", "spec", cmmd.Spec)
			err = r.createObject(ctx, cmmd, foundObj, objKind, reqLogger)
			if err != nil {
				reqLogger.Error(err, "Failed to create object")
				r.Recorder.Eventf(cmmd, corev1.EventTypeWarning, consts.EventReasonFailed, "Failed to create %s: %s", objKind, err)
				return ctrl.Result{}, err
			}

			reqLogger.Info("Object created", "kind", objKind)
			r.Recorder.Eventf(cmmd, corev1.EventTypeNormal, consts.EventReasonCreated, "Created %s", objKind)
			return ctrl.Result{Requeue: true}, nil
		}

		reqLogger.Error(err, "Failed to get object")
		r.Recorder.Eventf(cmmd, corev1.EventTypeWarning, consts.EventReasonFailed, "Failed to get %s: %s", objKind, err)
		return ctrl.Result{}, err
	}

	// ---------------------------------------------------------------------------------------------
	// 3. 변경사항 확인
	if ok, reason := r.needRecreate(foundObj, cmmd); ok {
		reqLogger.Info("Object recreate needed", "reason", reason)
		err = r.Delete(ctx, foundObj)
		if err != nil {
			reqLogger.Error(err, "Failed to delete object for recreate")
			r.Recorder.Eventf(cmmd, corev1.EventTypeWarning, consts.EventReasonFailed, "Failed to delete %s for recreate", objKind)
			return ctrl.Result{}, err
		}

		reqLogger.Info("Object deleted, new reconciling will be triggered")
		r.Recorder.Eventf(cmmd, corev1.EventTypeNormal, consts.EventReasonConfigured, "Deleted %s", objKind)
		return ctrl.Result{}, nil
	}

	// ---------------------------------------------------------------------------------------------
	// 4. CMMD Status 업데이트
	// update status
	err = r.updateStatus(ctx, cmmd, objKind)
	if err != nil {
		if errors.IsConflict(err) {
			logger.Info("Conflict CMMD status. Ignoring old resource version")
		}
		logger.Error(err, "Failed to update CMMD status")
		return ctrl.Result{}, err
	}

	reqLogger.Info("Reconciling done")
	return ctrl.Result{}, nil
}

// needRecreate
func (r *CMMDReconciler) needRecreate(obj client.Object, cmmd *cmmdv1.CMMD) (recreate bool, reason string) {
	anno := obj.GetAnnotations()

	if val, ok := anno[consts.AnnoReallocate]; ok {
		recreate = true
		reason = fmt.Sprintf("delivered annotation: %s", val)

	} else if anno == nil {
		recreate = true
		reason = "empty annotation"

	} else if anno[consts.AnnoSpecEnable] != strconv.FormatBool(cmmd.Spec.Enable) {
		recreate = true
		reason = "enable updated"

	} else if anno[consts.AnnoSpecAllocateMode] != cmmd.Spec.AllocateMode {
		recreate = true
		reason = "allocateMode updated"

	} else if anno[consts.AnnoSpecAllocateMode] == consts.AllocateModeManual {
		if anno[consts.AnnoSpecAllocateNode] != cmmd.Spec.Allocate.NodeName ||
			anno[consts.AnnoSpecAllocateMem] != cmmd.Spec.Allocate.Memory ||
			anno[consts.AnnoSpecAllocateCPU] != cmmd.Spec.Allocate.CPU {
			recreate = true
			reason = "allocate updated"
		}
	}

	return recreate, reason
}

// createObject
func (r *CMMDReconciler) createObject(ctx context.Context, cmmd *cmmdv1.CMMD, obj client.Object, kind string, reqLogger logr.Logger) error {
	// check most available node
	amountMemory := utils.GetAmountMemory(kind, obj)
	checker := node.NewChecker(&r.Client)
	allocatable, err := checker.NodeCheck(cmmd.Spec, float64(amountMemory.Value()))
	if err != nil {
		return err
	}
	reqLogger.Info("Allocating at", "node", allocatable.NodeName, "mem", allocatable.MemNuma, "cpu", allocatable.CPUList)

	// create
	r.setObject(cmmd, obj, kind, allocatable.NodeName, allocatable.MemNuma, allocatable.CPUList)
	err = r.Create(ctx, obj)
	if err != nil {
		return fmt.Errorf("create object error: %w", err)
	}

	return nil
}

// setObject
func (r *CMMDReconciler) setObject(cmmd *cmmdv1.CMMD, obj client.Object, kind, node, memNuma, cpuNuma string) {
	// set root fields of object (for compare whether CR modified or not)
	rootAnno := obj.GetAnnotations()
	if rootAnno == nil {
		rootAnno = map[string]string{}
	}
	rootAnno[consts.AnnoSpecEnable] = strconv.FormatBool(cmmd.Spec.Enable)
	rootAnno[consts.AnnoSpecAllocateMode] = cmmd.Spec.AllocateMode
	rootAnno[consts.AnnoSpecAllocateNode] = cmmd.Spec.Allocate.NodeName
	rootAnno[consts.AnnoSpecAllocateMem] = cmmd.Spec.Allocate.Memory
	rootAnno[consts.AnnoSpecAllocateCPU] = cmmd.Spec.Allocate.CPU
	obj.SetAnnotations(rootAnno)
	obj.SetNamespace(cmmd.Namespace)
	obj.SetName(cmmd.Name)

	// for pod
	podSpec, podAnno := r.getSpecAnnoOrLabels(obj, kind, true)
	podSpec.NodeName = node
	podAnno[consts.AnnoOwnerCR] = cmmd.Name
	podAnno[consts.AnnoSpecEnable] = strconv.FormatBool(cmmd.Spec.Enable)
	podAnno[consts.AnnoSpecAllocateMode] = cmmd.Spec.AllocateMode
	podAnno[consts.AnnoHookEnable] = strconv.FormatBool(cmmd.Spec.Enable)
	podAnno[consts.AnnoHookMemNuma] = memNuma
	podAnno[consts.AnnoHookCPUNuma] = cpuNuma
	r.setSpecAnnoOrLabels(obj, kind, podSpec, podAnno, true)

	// labels for memory allocatable (scheduling)
	memType := consts.LabelMemTypeValLocal
	if cmmd.Spec.Enable {
		memType = consts.LabelMemTypeValCMMD
	}
	_, podLabels := r.getSpecAnnoOrLabels(obj, kind, false)
	podLabels[consts.LabelAllocateNodeKey] = node
	podLabels[consts.LabelOperatorKey] = consts.LabelOperatorVal
	podLabels[consts.LabelMemTypeKey] = memType
	r.setSpecAnnoOrLabels(obj, kind, podSpec, podLabels, false)

	// set controller reference
	ctrl.SetControllerReference(cmmd, obj, r.Scheme)
}

// getSpecAnnoOrLabels
func (r *CMMDReconciler) getSpecAnnoOrLabels(obj client.Object, kind string, isAnno bool) (*corev1.PodSpec, map[string]string) {
	var spec corev1.PodSpec
	var annoOrLabels map[string]string

	switch kind {
	case consts.ObjectKindPod:
		o := obj.(*corev1.Pod)
		spec = o.Spec
		if isAnno {
			annoOrLabels = o.Annotations
		} else {
			annoOrLabels = o.Labels
		}

	case consts.ObjectKindDeployment:
		o := obj.(*appsv1.Deployment)
		spec = o.Spec.Template.Spec
		if isAnno {
			annoOrLabels = o.Spec.Template.Annotations
		} else {
			annoOrLabels = o.Spec.Template.Labels
		}

	case consts.ObjectKindReplicaset:
		o := obj.(*appsv1.ReplicaSet)
		spec = o.Spec.Template.Spec
		if isAnno {
			annoOrLabels = o.Spec.Template.Annotations
		} else {
			annoOrLabels = o.Spec.Template.Labels
		}
	}

	if annoOrLabels == nil {
		annoOrLabels = map[string]string{}
	}

	return &spec, annoOrLabels
}

// setSpecAnnoOrLabels
func (r *CMMDReconciler) setSpecAnnoOrLabels(obj client.Object, kind string, podSpec *corev1.PodSpec, podAnnoOrLabels map[string]string, isAnno bool) {
	switch kind {
	case consts.ObjectKindPod:
		o := obj.(*corev1.Pod)
		o.Spec = *podSpec
		if isAnno {
			o.Annotations = podAnnoOrLabels
		} else {
			o.Labels = podAnnoOrLabels
		}

	case consts.ObjectKindDeployment:
		o := obj.(*appsv1.Deployment)
		o.Spec.Template.Spec = *podSpec
		if isAnno {
			o.Spec.Template.Annotations = podAnnoOrLabels
		} else {
			o.Spec.Template.Labels = podAnnoOrLabels
		}

	case consts.ObjectKindReplicaset:
		o := obj.(*appsv1.ReplicaSet)
		o.Spec.Template.Spec = *podSpec
		if isAnno {
			o.Spec.Template.Annotations = podAnnoOrLabels
		} else {
			o.Spec.Template.Labels = podAnnoOrLabels
		}
	}
}

// updateStatus
func (r *CMMDReconciler) updateStatus(ctx context.Context, cmmd *cmmdv1.CMMD, kind string) error {
	amountMemory := utils.GetAmountMemory(kind, cmmd.Spec.Payload)
	cmmd.Status.Namespace = cmmd.Namespace
	cmmd.Status.Name = cmmd.Spec.Payload.GetName()
	cmmd.Status.Kind = cmmd.Spec.Payload.GetKind()
	cmmd.Status.Enable = cmmd.Spec.Enable
	cmmd.Status.AllocateMode = cmmd.Spec.AllocateMode
	cmmd.Status.NodeName = cmmd.Spec.Allocate.NodeName
	cmmd.Status.Memory = cmmd.Spec.Allocate.Memory
	cmmd.Status.CPU = cmmd.Spec.Allocate.CPU
	cmmd.Status.AmountMemory = amountMemory.Value()
	cmmd.Status.AmountMemoryStr = amountMemory.String()

	err := r.Status().Update(ctx, cmmd)
	return err
}

// SetupWithManager sets up the controller with the Manager.
func (r *CMMDReconciler) SetupWithManager(mgr ctrl.Manager) error {
	logger.Info("Start setup with manager")
	return ctrl.NewControllerManagedBy(mgr).
		For(&cmmdv1.CMMD{}).
		Owns(&corev1.Pod{}, builder.WithPredicates(r.predicates())).
		Owns(&appsv1.Deployment{}, builder.WithPredicates(r.predicates())).
		Owns(&appsv1.ReplicaSet{}, builder.WithPredicates(r.predicates())).
		Watches(
			&corev1.Pod{},
			handler.EnqueueRequestsFromMapFunc(r.watchHandler),
			builder.WithPredicates(r.predicatesWatch()),
		).
		Complete(r)
}

// predicates
func (r *CMMDReconciler) predicates() predicate.Predicate {
	return predicate.Funcs{
		CreateFunc:  func(e event.CreateEvent) bool { return false },
		UpdateFunc:  func(e event.UpdateEvent) bool { return false },
		DeleteFunc:  func(e event.DeleteEvent) bool { return e.Object.GetDeletionTimestamp().IsZero() },
		GenericFunc: func(e event.GenericEvent) bool { return false },
	}
}

// predicatesWatch predicates for watch
func (r *CMMDReconciler) predicatesWatch() predicate.Predicate {
	return predicate.Funcs{
		CreateFunc: func(e event.CreateEvent) bool {
			labels := e.Object.GetLabels()
			if labels == nil {
				return false
			}
			if labels[consts.LabelOperatorKey] != consts.LabelOperatorVal {
				return false
			}

			// 대상 CR 조건 filter : local memory 사용 / cmmd auto 사용
			anno := e.Object.GetAnnotations()
			if anno[consts.AnnoSpecEnable] == "false" ||
				(anno[consts.AnnoSpecEnable] == "true" && anno[consts.AnnoSpecAllocateMode] == consts.AllocateModeAuto) {
				return true
			}
			return false
		},
		UpdateFunc: func(e event.UpdateEvent) bool { return false },
		DeleteFunc: func(e event.DeleteEvent) bool {
			labels := e.Object.GetLabels()
			if labels == nil {
				return false
			}
			if labels[consts.LabelOperatorKey] != consts.LabelOperatorVal {
				return false
			}
			if e.Object.GetDeletionTimestamp().IsZero() {
				return false
			}

			anno := e.Object.GetAnnotations()
			anno["from"] = consts.AnnoHookWarmUp
			e.Object.SetAnnotations(anno)

			return true
		},
		GenericFunc: func(e event.GenericEvent) bool { return false },
	}
}

func (r *CMMDReconciler) makeWarmUpPod(ctx context.Context, obj client.Object, watchLogger logr.Logger) {
	pod := obj.(*corev1.Pod)
	for _, con := range pod.Status.ContainerStatuses {
		p := pod.DeepCopy()

		// p.Generation = 0
		// p.ResourceVersion = ""
		// p.UID = ""

		// p.Annotations[consts.AnnoHookWarmUp] = "true"
		// p.Annotations[consts.AnnoHookWarmUpType] = consts.WarmUpTypeDone
		// p.Annotations[consts.AnnoHookWarmUpCID] = con.ContainerID

		cid := con.ContainerID
		idx := strings.LastIndex(con.ContainerID, "://")
		if idx > 0 {
			cid = con.ContainerID[idx+3:]
		}
		p = &corev1.Pod{
			TypeMeta: metav1.TypeMeta{
				APIVersion: "v1",
				Kind:       "Pod",
			},
			ObjectMeta: metav1.ObjectMeta{
				Name:      p.Name,
				Namespace: p.Namespace,
				Annotations: map[string]string{
					consts.AnnoHookWarmUp:     consts.WarmUpTrue,
					consts.AnnoHookWarmUpCID:  cid,
					consts.AnnoHookWarmUpType: consts.WarmUpTypeDone,
				},
			},
			Spec: p.Spec,
		}

		if err := r.Client.Create(ctx, p); err != nil {
			watchLogger.Info("Failed to make warmup pod", "error", err)
		}
	}
}

// watchHandler pod 생성시 대상 node 가 배포불가 상태일때 재 스케줄링 받도록 request 생성
func (r *CMMDReconciler) watchHandler(ctx context.Context, obj client.Object) (requests []ctrl.Request) {
	logger.Info("====================================================================================================")
	anno := obj.GetAnnotations()
	if anno == nil {
		logger.Error(fmt.Errorf("empty annotaions"),
			"Failed to get owner name",
			"Watch.Namespace", obj.GetNamespace(),
			"Watch.Name", fmt.Sprintf("Unknown Owner(Pod:%s)", obj.GetName()),
		)
		return requests
	}

	ownerNamespace := obj.GetNamespace()
	ownerName := anno[consts.AnnoOwnerCR]

	watchLogger := logger.WithValues("Watch.Namespace", ownerNamespace, "Watch.Name", ownerName)
	watchLogger.Info("Watch handler start")

	// pod 종료시 crio 컨테이너 해제 버그 대응을 위해 warmup done pod 생성 호출
	if anno["from"] == consts.AnnoHookWarmUp {
		r.makeWarmUpPod(ctx, obj, watchLogger)
		return requests
	}

	// CR 조회
	cmmd := &cmmdv1.CMMD{}
	err := r.Get(ctx, types.NamespacedName{Namespace: ownerNamespace, Name: ownerName}, cmmd)
	if err != nil {
		watchLogger.Error(err, "Failed to get CMMD")
		r.Recorder.Eventf(cmmd, corev1.EventTypeWarning, consts.EventReasonFailedWatch, "Failed to get CMMD: %s", err)
		return requests
	}

	// 재배치 조건 조회
	pod := obj.(*corev1.Pod)
	reallocateReason, err := r.getReallocatableReason(ctx, *pod)
	if err != nil {
		watchLogger.Error(err, "Failed to check reallocatable reason")
		r.Recorder.Eventf(cmmd, corev1.EventTypeWarning, consts.EventReasonFailedWatch, "Failed to check reallocatable reason: %s", err)
		return requests
	}

	// Pod 를 재배치 할 이유가 있다면 reconciler 에서 재 스케줄링 되도록 Request 제출
	if reallocateReason != "" {
		watchLogger.Info("Reallocatable reason found", "reason", reallocateReason)
		// select payload object
		payObj, _, _ := utils.ConvertPayloadObject(cmmd.Spec.Payload)
		if err := r.Client.Get(ctx, types.NamespacedName{Namespace: ownerNamespace, Name: ownerName}, payObj); err != nil {
			if !errors.IsNotFound(err) {
				watchLogger.Error(err, "Failed to get payload object")
				r.Recorder.Eventf(cmmd, corev1.EventTypeWarning, consts.EventReasonFailedWatch, "Failed to get payload object: %s", err)
			}
			return requests
		}

		// add annotations and update
		payAnno := payObj.GetAnnotations()
		payAnno[consts.AnnoReallocate] = reallocateReason
		payObj.SetAnnotations(payAnno)

		if err := r.Update(ctx, payObj); err != nil {
			if !errors.IsConflict(err) {
				watchLogger.Error(err, "Failed to update object to reallocate node")
				r.Recorder.Eventf(cmmd, corev1.EventTypeWarning, consts.EventReasonFailedWatch, "Failed to update object to reallocate node: %s", err)
				return requests
			}
		}

		watchLogger.Info("Allocated node not available, new reconciling will be triggered")
		requests = append(requests, ctrl.Request{NamespacedName: types.NamespacedName{Namespace: ownerNamespace, Name: ownerName}})
	}

	watchLogger.Info("Watch handler done")
	return requests
}

// getReallocatableReason
func (r *CMMDReconciler) getReallocatableReason(ctx context.Context, pod corev1.Pod) (string, error) {
	var err error
	targetNode := pod.Spec.NodeName

	nodeList := &corev1.NodeList{}
	if err = r.Client.List(ctx, nodeList); err != nil {
		return "", fmt.Errorf("get node list error: %w", err)
	}

	var node *corev1.Node
	for _, item := range nodeList.Items {
		if item.Name == targetNode {
			node = &item
			break
		}
	}

	// 1.node not found
	if node == nil {
		return consts.ReallocateReasonNodeNotFound, nil
	}

	// 2.taint / toleration 확인
	permittedCnt := 0
	for _, taint := range node.Spec.Taints {
		for _, tol := range pod.Spec.Tolerations {
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
	if len(node.Spec.Taints) != permittedCnt {
		return consts.ReallocateReasonTolerations, nil
	}

	// 3.pod 가 배포 될 대상 node 상태 확인
	nodeReady := false
	for _, cond := range node.Status.Conditions {
		if cond.Type == corev1.NodeReady && cond.Status == corev1.ConditionTrue {
			nodeReady = true
		}
	}
	if !nodeReady {
		return consts.ReallocateReasonNodeNotReady, nil
	}

	return "", nil
}

// cmmdUnavailableHandler CMMD 장치 실패의 경우 pod 재배치 처리 핸들러. CMMD 차후 버전을 위한 인터페이스.
func (r *CMMDReconciler) cmmdUnavailableHandler(ctx context.Context, obj client.Object) error {
	logger.Info("====================================================================================================")
	eventLogger := logger.WithValues("Watch.Namespace", "", "Watch.Name", "")
	eventLogger.Info("Watch handler start")

	nodeName := "{node name from some event}" //TODO:: 향후 버전에서 인터페이스 작성시 node name 전달받는 방법 필요

	// 해당 노드에 할당된 pod 조회
	podList := &corev1.PodList{}
	labels := map[string]string{
		consts.LabelOperatorKey:     consts.LabelOperatorVal,
		consts.LabelAllocateNodeKey: nodeName,
	}
	listOpts := []client.ListOption{client.MatchingLabels(labels)}
	err := r.Client.List(ctx, podList, listOpts...)
	if err != nil {
		return fmt.Errorf("get pod list error: %w", err)
	}

	// owner 조회 후 payload update -> trigger reallocate
	for _, pod := range podList.Items {
		ownerNamespace := obj.GetNamespace()
		ownerName := pod.Annotations[consts.AnnoOwnerCR]

		// CR 조회
		cmmd := &cmmdv1.CMMD{}
		err := r.Get(ctx, types.NamespacedName{Namespace: ownerNamespace, Name: ownerName}, cmmd)
		if err != nil {
			eventLogger.Error(err, "Failed to get CMMD")
			r.Recorder.Eventf(cmmd, corev1.EventTypeWarning, consts.EventReasonFailedCMMD, "Failed to get CMMD: %s", err)
			return err
		}

		payObj, _, _ := utils.ConvertPayloadObject(cmmd.Spec.Payload)
		if err := r.Client.Get(ctx, types.NamespacedName{Namespace: ownerNamespace, Name: ownerName}, payObj); err != nil {
			eventLogger.Error(err, "Failed to get payload object")
			r.Recorder.Eventf(cmmd, corev1.EventTypeWarning, consts.EventReasonFailedWatch, "Failed to get payload object: %s", err)
			return err
		}

		// add annotations and update
		payAnno := payObj.GetAnnotations()
		payAnno[consts.AnnoReallocate] = consts.ReallocateReasonCMMDFail
		payObj.SetAnnotations(payAnno)
		if err := r.Update(ctx, payObj); err != nil {
			eventLogger.Error(err, "Failed to update object to reallocate node")
			r.Recorder.Eventf(cmmd, corev1.EventTypeWarning, consts.EventReasonFailedWatch, "Failed to update object to reallocate node: %s", err)
			return err
		}

	}

	return nil
}
