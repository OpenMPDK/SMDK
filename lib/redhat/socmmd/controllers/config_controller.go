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

	corev1 "k8s.io/api/core/v1"
	"k8s.io/apimachinery/pkg/runtime"
	"sigs.k8s.io/controller-runtime/pkg/client"
	"sigs.k8s.io/controller-runtime/pkg/event"

	"samsung/socmmd/internal/consts"
	"samsung/socmmd/internal/packageinfo"
	"samsung/socmmd/internal/patcher"

	"sigs.k8s.io/controller-runtime/pkg/predicate"
)

var (
	cfglogger = ctrl.Log.WithName("Config-Controller")
)

// ConfigReconciler reconciles a configmap object
type ConfigReconciler struct {
	client.Client
	Scheme *runtime.Scheme
}

func (r *ConfigReconciler) Reconcile(ctx context.Context, req ctrl.Request) (ctrl.Result, error) {
	cfglogger.Info("====================================================================================================")
	reqLogger := cfglogger.WithValues("Request.Namespace", req.Namespace, "Request.Name", req.Name)

	reqLogger.Info("Config reconcile start")
	patcher.PatchDaemonset(r.Client, r.Scheme)
	reqLogger.Info("Config reconcile done")

	return ctrl.Result{}, nil
}

// SetupWithManager sets up the controller with the Manager.
func (r *ConfigReconciler) SetupWithManager(mgr ctrl.Manager) error {
	cfglogger.Info("Start setup with config manager")
	return ctrl.NewControllerManagedBy(mgr).
		For(&corev1.ConfigMap{}).
		WithEventFilter(r.predicates()).
		Complete(r)
}

// predicates
func (r *ConfigReconciler) predicates() predicate.Predicate {
	return predicate.Funcs{
		CreateFunc:  func(e event.CreateEvent) bool { return r.checkCondition(e.Object) },
		UpdateFunc:  func(e event.UpdateEvent) bool { return r.checkCondition(e.ObjectNew) },
		DeleteFunc:  func(e event.DeleteEvent) bool { return r.checkCondition(e.Object) },
		GenericFunc: func(e event.GenericEvent) bool { return r.checkCondition(e.Object) },
	}
}

// checkCondition
func (r *ConfigReconciler) checkCondition(obj client.Object) bool {
	if obj.GetNamespace() == packageinfo.GetNamespace() && obj.GetName() == consts.ConfigMapName {
		return true
	}
	return false
}
