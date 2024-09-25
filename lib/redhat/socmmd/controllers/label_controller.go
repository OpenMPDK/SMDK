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

	cmmdv1 "samsung/socmmd/api/v1"
	"samsung/socmmd/internal/consts"
	"samsung/socmmd/internal/patcher"

	corev1 "k8s.io/api/core/v1"
	"k8s.io/apimachinery/pkg/api/errors"
	"k8s.io/apimachinery/pkg/runtime"
	"k8s.io/client-go/tools/record"
	ctrl "sigs.k8s.io/controller-runtime"

	"sigs.k8s.io/controller-runtime/pkg/client"
	"sigs.k8s.io/controller-runtime/pkg/event"

	"sigs.k8s.io/controller-runtime/pkg/predicate"
)

var (
	labelLogger = ctrl.Log.WithName("Label-Controller")
)

// LabelReconciler reconciles a Label object
type LabelReconciler struct {
	client.Client
	Scheme   *runtime.Scheme
	Recorder record.EventRecorder
}

func (r *LabelReconciler) Reconcile(ctx context.Context, req ctrl.Request) (ctrl.Result, error) {
	labelLogger.Info("====================================================================================================")
	reqLogger := labelLogger.WithValues("Request.Namespace", req.Namespace, "Request.Name", req.Name)
	reqLogger.Info("Labeling start")

	// Label 조회
	label := &cmmdv1.Label{}
	err := r.Get(ctx, req.NamespacedName, label)
	if err != nil {
		if errors.IsNotFound(err) {
			reqLogger.Info("Label resource not found. Ignoring since object must be deleted. Trying to delete MC.")
			if err := patcher.DeleteMC(r.Client); err != nil {
				reqLogger.Error(err, "Failed to delete MC, skipped error.")
			}
			return ctrl.Result{}, nil
		}
		reqLogger.Error(err, "Failed to get Label")
		r.Recorder.Eventf(label, corev1.EventTypeWarning, consts.EventReasonFailedCR, "Failed to get Label: %s", err)
		return ctrl.Result{}, err
	}

	// prdicate phase 별 mc 처리
	switch label.Annotations[consts.AnnoPredicatesPhase] {
	case consts.PredicatesPhaseCreate:
		// create
		reqLogger.Info("Label created")
		r.Recorder.Event(label, corev1.EventTypeNormal, consts.EventReasonCreated, "Label created")
		if len(label.Labels) == 0 {
			reqLogger.Info("No labels assigned, skipped create MC")
			break
		}
		if err := patcher.CreateMC(r.Client, r.Scheme, label.Labels); err != nil {
			reqLogger.Error(err, "Failed to create MC")
			r.Recorder.Event(label, corev1.EventTypeWarning, consts.EventReasonFailed, "Failed to create MC")
			return ctrl.Result{}, err
		}
		reqLogger.Info("MC created")
		r.Recorder.Event(label, corev1.EventTypeNormal, consts.EventReasonCreated, "MC created")

	case consts.PredicatesPhaseUpdate:
		// update
		reqLogger.Info("Label modified")
		if err := patcher.DeleteMC(r.Client); err != nil {
			reqLogger.Error(err, "Failed to delete MC")
			r.Recorder.Event(label, corev1.EventTypeWarning, consts.EventReasonFailed, "Failed to delete MC")
			return ctrl.Result{}, err
		}
		if len(label.Labels) == 0 {
			reqLogger.Info("No labels assigned, skipped create MC")
			break
		}
		if err := patcher.CreateMC(r.Client, r.Scheme, label.Labels); err != nil {
			reqLogger.Error(err, "Failed to create MC")
			r.Recorder.Event(label, corev1.EventTypeWarning, consts.EventReasonFailed, "Failed to create MC")
			return ctrl.Result{}, err
		}
		reqLogger.Info("MC modified")
		r.Recorder.Event(label, corev1.EventTypeNormal, consts.EventReasonModified, "MC modified")

	case consts.PredicatesPhaseDelete:
		// delete
		reqLogger.Info("Label deleted")
		if err := patcher.DeleteMC(r.Client); err != nil {
			reqLogger.Error(err, "Failed to delete MC")
			return ctrl.Result{}, err
		}
	}

	reqLogger.Info("Labeling done")
	return ctrl.Result{}, nil
}

// SetupWithManager sets up the controller with the Manager.
func (r *LabelReconciler) SetupWithManager(mgr ctrl.Manager) error {
	labelLogger.Info("Start setup with label manager")
	return ctrl.NewControllerManagedBy(mgr).
		For(&cmmdv1.Label{}).
		WithEventFilter(r.predicates()).
		Complete(r)
}

// predicates
func (r *LabelReconciler) predicates() predicate.Predicate {
	return predicate.Funcs{
		CreateFunc: func(e event.CreateEvent) bool {
			e.Object.SetAnnotations(r.getAnno(consts.PredicatesPhaseCreate))
			return e.Object.GetName() == consts.NodeLabel
		},
		UpdateFunc: func(e event.UpdateEvent) bool {
			e.ObjectNew.SetAnnotations(r.getAnno(consts.PredicatesPhaseUpdate))
			return e.ObjectNew.GetName() == consts.NodeLabel
		},
		DeleteFunc: func(e event.DeleteEvent) bool {
			e.Object.SetAnnotations(r.getAnno(consts.PredicatesPhaseDelete))
			return e.Object.GetName() == consts.NodeLabel
		},
		GenericFunc: func(e event.GenericEvent) bool {
			return false
		},
	}
}

func (r *LabelReconciler) getAnno(phase string) map[string]string {
	return map[string]string{consts.AnnoPredicatesPhase: phase}
}
