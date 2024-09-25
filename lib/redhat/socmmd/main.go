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

package main

import (
	"context"
	"flag"
	"os"

	// Import all Kubernetes client auth plugins (e.g. Azure, GCP, OIDC, etc.)
	// to ensure that exec-entrypoint and run can make use of them.
	_ "k8s.io/client-go/plugin/pkg/client/auth"

	cmmdv1 "samsung/socmmd/api/v1"
	"samsung/socmmd/controllers"
	"samsung/socmmd/internal/assets"
	"samsung/socmmd/internal/consts"
	"samsung/socmmd/internal/packageinfo"
	"samsung/socmmd/internal/patcher"

	consolev1 "github.com/openshift/api/console/v1"
	operv1 "github.com/openshift/api/operator/v1"
	mcfgv1 "github.com/openshift/machine-config-operator/pkg/apis/machineconfiguration.openshift.io/v1"
	opfv1 "github.com/operator-framework/api/pkg/operators/v1alpha1"
	"k8s.io/apimachinery/pkg/runtime"
	utilruntime "k8s.io/apimachinery/pkg/util/runtime"
	clientgoscheme "k8s.io/client-go/kubernetes/scheme"
	ctrl "sigs.k8s.io/controller-runtime"
	"sigs.k8s.io/controller-runtime/pkg/client"
	"sigs.k8s.io/controller-runtime/pkg/healthz"
	"sigs.k8s.io/controller-runtime/pkg/log/zap"
	"sigs.k8s.io/controller-runtime/pkg/manager"
	//+kubebuilder:scaffold:imports
)

var (
	scheme   = runtime.NewScheme()
	setupLog = ctrl.Log.WithName("setup")
)

func init() {
	utilruntime.Must(clientgoscheme.AddToScheme(scheme))

	utilruntime.Must(cmmdv1.AddToScheme(scheme))
	utilruntime.Must(mcfgv1.AddToScheme(scheme))
	utilruntime.Must(opfv1.AddToScheme(scheme))
	utilruntime.Must(operv1.AddToScheme(scheme))
	utilruntime.Must(consolev1.AddToScheme(scheme))
	//+kubebuilder:scaffold:scheme
}

func main() {
	var metricsAddr string
	var enableLeaderElection bool
	var probeAddr string
	flag.StringVar(&metricsAddr, "metrics-bind-address", ":8080", "The address the metric endpoint binds to.")
	flag.StringVar(&probeAddr, "health-probe-bind-address", ":8081", "The address the probe endpoint binds to.")
	flag.BoolVar(&enableLeaderElection, "leader-elect", false,
		"Enable leader election for controller manager. "+
			"Enabling this will ensure there is only one active controller manager.")
	opts := zap.Options{
		Development: true,
	}
	opts.BindFlags(flag.CommandLine)
	flag.Parse()

	ctrl.SetLogger(zap.New(zap.UseFlagOptions(&opts)))

	// init manager
	mgr, err := ctrl.NewManager(ctrl.GetConfigOrDie(), ctrl.Options{
		Scheme:                 scheme,
		MetricsBindAddress:     metricsAddr,
		Port:                   9443,
		HealthProbeBindAddress: probeAddr,
		LeaderElection:         enableLeaderElection,
		LeaderElectionID:       "be9d7b6e.samsung.com",
		// LeaderElectionReleaseOnCancel defines if the leader should step down voluntarily
		// when the Manager ends. This requires the binary to immediately end when the
		// Manager is stopped, otherwise, this setting is unsafe. Setting this significantly
		// speeds up voluntary leader transitions as the new leader don't have to wait
		// LeaseDuration time first.
		//
		// In the default scaffold provided, the program ends immediately after
		// the manager stops, so would be fine to enable this option. However,
		// if you are doing or is intended to do any operation such as perform cleanups
		// after the manager stops then its usage might be unsafe.
		// LeaderElectionReleaseOnCancel: true,
	})
	if err != nil {
		setupLog.Error(err, "Unable to start manager")
		os.Exit(1)
	}

	// cmmd reconciler
	if err = (&controllers.CMMDReconciler{
		Client:   mgr.GetClient(),
		Scheme:   mgr.GetScheme(),
		Recorder: mgr.GetEventRecorderFor(consts.Manager),
	}).SetupWithManager(mgr); err != nil {
		setupLog.Error(err, "Unable to create controller", "controller", "CMMD")
		os.Exit(1)
	}

	// label reconciler
	if err = (&controllers.LabelReconciler{
		Client:   mgr.GetClient(),
		Scheme:   mgr.GetScheme(),
		Recorder: mgr.GetEventRecorderFor(consts.Manager),
	}).SetupWithManager(mgr); err != nil {
		setupLog.Error(err, "Unable to create controller", "controller", "Label")
		os.Exit(1)
	}

	// csv reconciler for cleanup when operator deleted
	if err = (&controllers.CSVReconciler{
		Client: mgr.GetClient(),
	}).SetupWithManager(mgr); err != nil {
		setupLog.Error(err, "Unable to create controller", "controller", "Cleanup")
		os.Exit(1)
	}

	// warmup reconciler
	if err = (&controllers.WarmUpReconciler{
		Client:   mgr.GetClient(),
		Recorder: mgr.GetEventRecorderFor(consts.Manager),
	}).SetupWithManager(mgr); err != nil {
		setupLog.Error(err, "Unable to create controller", "controller", "WarmUp")
		os.Exit(1)
	}

	// config reconciler
	if err = (&controllers.ConfigReconciler{
		Client: mgr.GetClient(),
		Scheme: mgr.GetScheme(),
	}).SetupWithManager(mgr); err != nil {
		setupLog.Error(err, "Unable to create controller", "controller", "Config")
		os.Exit(1)
	}

	//+kubebuilder:scaffold:builder

	if err := mgr.Add(prepare(&mgr)); err != nil {
		setupLog.Error(err, "Unable to set up patcher")
		os.Exit(1)
	}

	if err := mgr.AddHealthzCheck("healthz", healthz.Ping); err != nil {
		setupLog.Error(err, "Unable to set up health check")
		os.Exit(1)
	}
	if err := mgr.AddReadyzCheck("readyz", healthz.Ping); err != nil {
		setupLog.Error(err, "Unable to set up ready check")
		os.Exit(1)
	}

	setupLog.Info("starting manager")
	if err := mgr.Start(ctrl.SetupSignalHandler()); err != nil {
		setupLog.Error(err, "Problem running manager")
		os.Exit(1)
	}
}

// prepare
type runner struct {
	client client.Client
	scheme *runtime.Scheme
}

func prepare(mgr *manager.Manager) manager.Runnable {
	r := runner{
		client: (*mgr).GetClient(),
		scheme: (*mgr).GetScheme(),
	}
	return &r
}
func (r *runner) Start(ctx context.Context) error {
	setupLog.Info("Prepairing start...")

	// package info
	setupLog.Info("Package info init start...")
	if err := packageinfo.Init(r.client); err != nil {
		return err
	}
	setupLog.Info("Package info init done", "data", packageinfo.String())

	// assets
	setupLog.Info("Assets init start...")
	if err := assets.Init(r.scheme); err != nil {
		return err
	}
	setupLog.Info("Assets init done")

	// patcher
	setupLog.Info("Patcher start...")
	if err := patcher.Run(r.client, r.scheme); err != nil {
		return err
	}
	setupLog.Info("Patcher done")

	setupLog.Info("Prepairing done")
	return nil
}
