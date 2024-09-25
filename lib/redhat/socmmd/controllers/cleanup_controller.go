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

	ctrl "sigs.k8s.io/controller-runtime"

	cmmdv1 "samsung/socmmd/api/v1"

	"github.com/go-logr/logr"
	"sigs.k8s.io/controller-runtime/pkg/client"
	"sigs.k8s.io/controller-runtime/pkg/event"

	"samsung/socmmd/internal/consts"
	"samsung/socmmd/internal/packageinfo"
	"samsung/socmmd/internal/patcher"

	opfv1 "github.com/operator-framework/api/pkg/operators/v1alpha1"

	"sigs.k8s.io/controller-runtime/pkg/predicate"
)

var (
	wlogger = ctrl.Log.WithName("Cleanup-Controller")
)

// CSVReconciler reconciles a CSV object
type CSVReconciler struct {
	client.Client
}

func (r *CSVReconciler) Reconcile(ctx context.Context, req ctrl.Request) (ctrl.Result, error) {
	wlogger.Info("====================================================================================================")
	reqLogger := wlogger.WithValues("Request.Namespace", req.Namespace, "Request.Name", req.Name)

	reqLogger.Info("Cleaning start")
	patcher.Cleanup(r.Client)
	r.cleanupNodeLabel(ctx, reqLogger)
	reqLogger.Info("Cleaning done")

	return ctrl.Result{}, nil
}

// cleanupNodeLabel
func (r *CSVReconciler) cleanupNodeLabel(ctx context.Context, reqLogger logr.Logger) {
	label := &cmmdv1.Label{}
	label.Namespace = packageinfo.GetNamespace()
	label.Name = consts.NodeLabel
	if err := r.Client.Delete(ctx, label); err != nil {
		reqLogger.Error(err, "Failed to delete node label")
	}
}

// SetupWithManager sets up the controller with the Manager.
func (r *CSVReconciler) SetupWithManager(mgr ctrl.Manager) error {
	wlogger.Info("Start setup with cleanup manager")
	return ctrl.NewControllerManagedBy(mgr).
		For(&opfv1.ClusterServiceVersion{}).
		WithEventFilter(r.predicates()).
		Complete(r)
}

// predicates
func (r *CSVReconciler) predicates() predicate.Predicate {
	return predicate.Funcs{
		CreateFunc: func(e event.CreateEvent) bool { return false },
		UpdateFunc: func(e event.UpdateEvent) bool {
			if e.ObjectNew.GetNamespace() == packageinfo.GetNamespace() &&
				e.ObjectNew.GetName() == packageinfo.GetNameVersion() &&
				!e.ObjectNew.GetDeletionTimestamp().IsZero() {
				return true
			}
			return false
		},
		DeleteFunc:  func(e event.DeleteEvent) bool { return false },
		GenericFunc: func(e event.GenericEvent) bool { return false },
	}
}
