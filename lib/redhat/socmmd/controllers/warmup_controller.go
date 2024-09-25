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

	"k8s.io/apimachinery/pkg/api/errors"
	"k8s.io/client-go/tools/record"
	ctrl "sigs.k8s.io/controller-runtime"

	"sigs.k8s.io/controller-runtime/pkg/client"
	"sigs.k8s.io/controller-runtime/pkg/event"

	"samsung/socmmd/internal/consts"

	corev1 "k8s.io/api/core/v1"
	metav1 "k8s.io/apimachinery/pkg/apis/meta/v1"
	"sigs.k8s.io/controller-runtime/pkg/predicate"
)

var (
	wulogger = ctrl.Log.WithName("WarmUp-Controller")
)

// WarmUpReconciler reconciles a warmup pod
type WarmUpReconciler struct {
	client.Client
	Recorder record.EventRecorder
}

func (r *WarmUpReconciler) Reconcile(ctx context.Context, req ctrl.Request) (ctrl.Result, error) {
	reqLogger := wulogger.WithValues("Request.Namespace", req.Namespace, "Request.Name", req.Name)

	// WarmUp pod
	pod := &corev1.Pod{}
	err := r.Get(ctx, req.NamespacedName, pod)
	if err != nil {
		if !errors.IsNotFound(err) {
			reqLogger.Error(err, "Failed to get WarmUp")
			r.Recorder.Eventf(pod, corev1.EventTypeWarning, consts.EventReasonFailedWarmUp, "Failed to get WarmUp: %s", err)
			return ctrl.Result{}, err
		}
	}

	// info
	hostname := pod.Spec.NodeName
	warmUpType := pod.Annotations[consts.AnnoHookWarmUpType]

	if warmUpType == consts.WarmUpTypeStart {
		// ----------------------------------------------------------------------------
		// WarmUp phase : start
		for _, con := range pod.Status.ContainerStatuses {
			// WarmUp-Start pod 삭제
			if con.State.Running != nil && !con.State.Running.StartedAt.IsZero() {
				if err := r.Delete(ctx, pod); err != nil {
					reqLogger.Error(err, "Failed to delete WarmUp-Start", "hostname", hostname)
					r.Recorder.Eventf(pod, corev1.EventTypeWarning, consts.EventReasonFailedWarmUp, "Failed to delete WarmUp-Start: %s: %s", hostname, err)
					return ctrl.Result{}, err
				}
				reqLogger.Info("Deleting WarmUp-Start", "hostname", hostname)
			}
			// WarmUp-Done pod 생성
			if con.State.Terminated != nil && !con.State.Terminated.FinishedAt.IsZero() {
				donePod := r.getDonePod(pod)
				if err := r.Client.Create(ctx, donePod); err != nil {
					if !errors.IsAlreadyExists(err) {
						reqLogger.Error(err, "Failed to create WarmUp-Done")
						r.Recorder.Eventf(pod, corev1.EventTypeWarning, consts.EventReasonFailedWarmUp, "Failed to create WarmUp-Done: %s", err)
						return ctrl.Result{}, err
					}
				}
				reqLogger.Info("Creating WarmUp-Done", "hostname", hostname)
			}
		}

	} else if warmUpType == consts.WarmUpTypeDone {
		// ----------------------------------------------------------------------------
		// WarmUp phase : done
		for _, con := range pod.Status.ContainerStatuses {
			if con.State.Running != nil {
				// WarmUp-Done pod 삭제
				pod := &corev1.Pod{}
				err := r.Get(ctx, req.NamespacedName, pod)
				if err != nil {
					if !errors.IsNotFound(err) {
						reqLogger.Error(err, "Failed to get WarmUp-Done")
						r.Recorder.Eventf(pod, corev1.EventTypeWarning, consts.EventReasonFailedWarmUp, "Failed to get WarmUp-Done: %s", err)
						return ctrl.Result{}, err
					}
				}
				if err := r.Delete(ctx, pod); err != nil {
					reqLogger.Error(err, "Failed to delete WarmUp-Done", "hostname", hostname)
					r.Recorder.Eventf(pod, corev1.EventTypeWarning, consts.EventReasonFailedWarmUp, "Failed to delete WarmUp-Done: %s: %s", hostname, err)
					return ctrl.Result{}, err
				}
				reqLogger.Info("Deleting WarmUp-Done", "hostname", hostname)
			}
		}
	}

	return ctrl.Result{}, nil
}

// SetupWithManager sets up the controller with the Manager.
func (r *WarmUpReconciler) SetupWithManager(mgr ctrl.Manager) error {
	wulogger.Info("Start setup with warmup manager")
	return ctrl.NewControllerManagedBy(mgr).
		For(&corev1.Pod{}).
		WithEventFilter(r.predicates()).
		Complete(r)
}

// predicates
func (r *WarmUpReconciler) predicates() predicate.Predicate {
	return predicate.Funcs{
		CreateFunc:  func(e event.CreateEvent) bool { return false },
		UpdateFunc:  func(e event.UpdateEvent) bool { return r.hasAnnotation(e.ObjectNew) },
		DeleteFunc:  func(e event.DeleteEvent) bool { return false },
		GenericFunc: func(e event.GenericEvent) bool { return false },
	}
}

// hasAnnotation
func (r *WarmUpReconciler) hasAnnotation(obj client.Object) bool {
	anno := obj.GetAnnotations()
	if anno == nil {
		return false
	}
	if val, ok := anno[consts.AnnoHookWarmUp]; !ok && val == consts.WarmUpTrue {
		return false
	}
	return true
}

// getDonePod
func (r *WarmUpReconciler) getDonePod(pod *corev1.Pod) *corev1.Pod {
	return &corev1.Pod{
		TypeMeta: metav1.TypeMeta{
			APIVersion: "v1",
			Kind:       "Pod",
		},
		ObjectMeta: metav1.ObjectMeta{
			Name:      pod.Name + "-done",
			Namespace: pod.Namespace,
			Annotations: map[string]string{
				consts.AnnoHookWarmUp:     consts.WarmUpTrue,
				consts.AnnoHookWarmUpType: consts.WarmUpTypeDone,
			},
		},
		Spec: pod.Spec,
	}
}
