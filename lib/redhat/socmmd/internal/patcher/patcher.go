package patcher

import (
	"context"
	"fmt"
	"sync"

	"gopkg.in/yaml.v2"
	"k8s.io/apimachinery/pkg/api/errors"
	v1 "k8s.io/apimachinery/pkg/apis/meta/v1"

	"samsung/socmmd/internal/assets"
	"samsung/socmmd/internal/consts"
	"samsung/socmmd/internal/packageinfo"

	consolev1 "github.com/openshift/api/console/v1"
	operv1 "github.com/openshift/api/operator/v1"
	mcfgv1 "github.com/openshift/machine-config-operator/pkg/apis/machineconfiguration.openshift.io/v1"
	appsv1 "k8s.io/api/apps/v1"
	corev1 "k8s.io/api/core/v1"
	rbacv1 "k8s.io/api/rbac/v1"
	"k8s.io/apimachinery/pkg/runtime"
	"k8s.io/apimachinery/pkg/runtime/serializer"
	"k8s.io/apimachinery/pkg/types"
	ctrl "sigs.k8s.io/controller-runtime"
	"sigs.k8s.io/controller-runtime/pkg/client"
)

var (
	logger               = ctrl.Log.WithName("Patcher")
	createdObjects       = []client.Object{}
	daemonsetServiceName = ""
)

type patcher struct {
	client client.Client
	scheme *runtime.Scheme
}

func Run(c client.Client, s *runtime.Scheme) error {
	p := patcher{
		client: c,
		scheme: s,
	}
	return p.run()
}

// run
func (r *patcher) run() error {
	if err := r.patchObject(assets.GetConfigMaps(), true); err != nil {
		return err
	}
	if err := r.patchObject(assets.GetServicesAccount(), true); err != nil {
		return err
	}
	if err := r.patchObject(assets.GetClusterRole(), true); err != nil {
		return err
	}
	if err := r.patchObject(assets.GetClusterRoleBinding(), true); err != nil {
		return err
	}
	if err := r.patchObject(assets.GetDaemonsets(), true); err != nil {
		return err
	}
	if err := r.patchObject(assets.GetDeployment(), true); err != nil {
		return err
	}
	if err := r.patchObject(assets.GetConsole(), true); err != nil {
		return err
	}
	if err := r.patchObject(assets.GetConsolePlugin(), true); err != nil {
		return err
	}
	if err := r.patchObject(assets.GetServices(), true); err != nil {
		return err
	}

	// set additional task after patch
	r.postPatch()

	return nil
}

// patchObject
func (r *patcher) patchObject(contents []assets.Content, appendCleanupList bool) error {
	for _, content := range contents {
		obj, err := r.getInstalledObject(content)
		if err != nil {
			return err
		}

		if err := r.createOrUpdate(content, obj); err != nil {
			return err
		}

		if appendCleanupList {
			if err = r.appendCleanupList(content); err != nil {
				return err
			}
		}
	}

	return nil
}

// getInstalledObject
func (r *patcher) getInstalledObject(content assets.Content) (client.Object, error) {
	obj := r.getEmptyObject(content)
	if obj == nil {
		return nil, fmt.Errorf("not supported object kind: %s", content.Kind)
	}

	err := r.client.Get(context.TODO(), types.NamespacedName{Namespace: content.Namespace, Name: content.Name}, obj)
	if err != nil {
		if errors.IsNotFound(err) {
			return nil, nil
		}
		return nil, fmt.Errorf("get error: [%s]%s: %w", content.Kind, content.Name, err)
	}
	return obj, nil
}

// getEmptyObject
func (r *patcher) getEmptyObject(content assets.Content) client.Object {
	var obj client.Object
	typeMeta := v1.TypeMeta{Kind: content.Kind}
	switch content.Kind {
	case consts.ObjectKindDeployment:
		obj = &appsv1.Deployment{TypeMeta: typeMeta}
	case consts.ObjectKindDaemonset:
		obj = &appsv1.DaemonSet{TypeMeta: typeMeta}
	case consts.ObjectKindService:
		obj = &corev1.Service{TypeMeta: typeMeta}
	case consts.ObjectKindServiceAccount:
		obj = &corev1.ServiceAccount{TypeMeta: typeMeta}
	case consts.ObjectKindClusterRole:
		obj = &rbacv1.ClusterRole{TypeMeta: typeMeta}
	case consts.ObjectKindClusterRoleBinding:
		obj = &rbacv1.ClusterRoleBinding{TypeMeta: typeMeta}
	case consts.ObjectKindConfigMap:
		obj = &corev1.ConfigMap{TypeMeta: typeMeta}
	case consts.ObjectKindConsole:
		obj = &operv1.Console{TypeMeta: typeMeta}
	case consts.ObjectKindConsolePlugin:
		obj = &consolev1.ConsolePlugin{TypeMeta: typeMeta}
	case consts.ObjectKindMC:
		obj = &mcfgv1.MachineConfig{TypeMeta: typeMeta}
	default:
		obj = nil
	}
	if obj != nil {
		obj.SetNamespace(content.Namespace)
		obj.SetName(content.Name)
	}
	return obj
}

// createObject
func (r *patcher) createOrUpdate(content assets.Content, installedObj client.Object) error {
	// get object to apply
	obj, err := r.getConfiguredObject(content)
	if err != nil {
		return fmt.Errorf("configure object error: [%s][%s]%s: %w", content.Namespace, content.Kind, content.Name, err)
	}

	// check create or update
	needUpdate, err := r.checkNeedUpdate(content, installedObj)
	if err != nil {
		return fmt.Errorf("check need update error: [%s][%s]%s: %w", content.Namespace, content.Kind, content.Name, err)
	}

	// create or update
	if needUpdate {
		logger.Info("Object exists, trying to update", "kind", content.Kind, "name", content.Name)
		if err = r.client.Update(context.TODO(), obj); err != nil {
			return fmt.Errorf("update object error: [%s][%s]%s: %w", content.Namespace, content.Kind, content.Name, err)
		}

	} else {
		logger.Info("Creating object", "kind", content.Kind, "name", content.Name)
		if err = r.client.Create(context.TODO(), obj); err != nil {
			return fmt.Errorf("create object error: [%s][%s]%s: %w", content.Namespace, content.Kind, content.Name, err)
		}
	}

	return nil
}

// setDaemonsetTolerations
func (r *patcher) setDaemonsetTolerations(obj client.Object) (client.Object, error) {
	// get mc
	cmNamespace := packageinfo.GetNamespace()
	cmName := "cmmd-config"
	cmKey := "CMMD_ND_TOLERATIONS"
	cm := &corev1.ConfigMap{}
	key := types.NamespacedName{Namespace: cmNamespace, Name: cmName}
	err := r.client.Get(context.TODO(), key, cm)
	if err != nil {
		if !errors.IsNotFound(err) {
			return nil, fmt.Errorf("get toleration configmap error: %w", err)
		}
		cm = &corev1.ConfigMap{}
	}

	tolerations := []corev1.Toleration{}
	for k, v := range cm.Data {
		if k != cmKey {
			continue
		}
		if err := yaml.Unmarshal([]byte(v), &tolerations); err != nil {
			return nil, fmt.Errorf("unmarshal toleration error: %w", err)
		}
	}

	ds := obj.(*appsv1.DaemonSet)
	ds.Spec.Template.Spec.Tolerations = tolerations

	return ds, nil
}

// setNamespace
func (r *patcher) setNamespace(content assets.Content, obj client.Object) client.Object {
	obj.SetNamespace(content.Namespace)

	switch content.Kind {
	case consts.ObjectKindClusterRoleBinding:
		o := obj.(*rbacv1.ClusterRoleBinding)
		for i := range o.Subjects {
			o.Subjects[i].Namespace = content.Namespace
		}
		obj = o
	case consts.ObjectKindConsolePlugin:
		o := obj.(*consolev1.ConsolePlugin)
		o.Spec.Backend.Service.Namespace = content.Namespace
		obj = o
	}

	return obj
}

// appendCleanupList
func (r *patcher) appendCleanupList(content assets.Content) error {
	obj := r.getEmptyObject(content)
	if obj == nil {
		return fmt.Errorf("append nil object error")
	}
	createdObjects = append(createdObjects, obj)
	return nil
}

// checkNeedUpdate
func (r *patcher) checkNeedUpdate(content assets.Content, installedObj client.Object) (bool, error) {
	// 특정 object 는 update 에 제약사항이 있으므로 재설치 할 수 있도록 삭제
	if content.Kind == consts.ObjectKindConsole || content.Kind == consts.ObjectKindConsolePlugin {
		if installedObj != nil {
			logger.Info("Deleting object", "kind", content.Kind, "name", content.Name)
			if err := r.client.Delete(context.TODO(), installedObj); err != nil {
				return false, fmt.Errorf("delete object error: [%s][%s]%s: %w", content.Namespace, content.Kind, content.Name, err)
			}
			installedObj = nil
		}
	}

	return installedObj != nil, nil
}

// getConfiguredObject
func (r *patcher) getConfiguredObject(content assets.Content) (client.Object, error) {
	var err error
	obj := decodeObject(content.Body, r.scheme)

	// set namespace
	obj = r.setNamespace(content, obj)

	// set daemonset tolerations
	if content.Kind == consts.ObjectKindDaemonset {
		if obj, err = r.setDaemonsetTolerations(obj); err != nil {
			return nil, fmt.Errorf("set daemonset toleration error: %w", err)
		}
	}

	return obj, nil
}

// postPatch
func (r *patcher) postPatch() error {
	// 1.daemonset service name 저장 - node checker 에서 service endpoint 조회시 daemonset 의 service name 필요
	for _, dsContent := range assets.GetDaemonsets() {
		dsObj := decodeObject(dsContent.Body, r.scheme)
		ds := dsObj.(*appsv1.DaemonSet)
		for _, svcContent := range assets.GetServices() {
			svcObj := decodeObject(svcContent.Body, r.scheme)
			svc := svcObj.(*corev1.Service)

			matchCnt := 0
			for svcK, svcV := range svc.Spec.Selector {
				if label, ok := ds.Spec.Template.Labels[svcK]; ok && label == svcV {
					matchCnt++
				}
			}

			if matchCnt == len(svc.Spec.Selector) {
				daemonsetServiceName = svc.Name
			}
		}
	}

	return nil
}

// decodeObject
func decodeObject(body []byte, scheme *runtime.Scheme) client.Object {
	decode := serializer.NewCodecFactory(scheme).UniversalDeserializer().Decode
	o, _, _ := decode(body, nil, nil)
	return o.(client.Object)
}

// Cleanup
func Cleanup(client client.Client) {
	wg := sync.WaitGroup{}
	for _, obj := range createdObjects {
		wg.Add(1)
		go func() {
			defer wg.Done()
			logger.Info("Cleaning object", "kind", obj.GetObjectKind().GroupVersionKind().Kind, "name", obj.GetName(), "namespace", obj.GetNamespace())
			if err := client.Delete(context.TODO(), obj); err != nil && !errors.IsNotFound(err) {
				logger.Info("Cleaning error, failed to delete object", "kind", obj.GetObjectKind().GroupVersionKind().Kind, "name", obj.GetName(), "namespace", obj.GetNamespace(), "error", err)
			}
		}()
	}
	wg.Wait()
}

// DeleteMC
func DeleteMC(c client.Client) error {
	p := patcher{}
	mcList := []client.Object{}
	for _, content := range assets.GetMCs() {
		mc := p.getEmptyObject(content)
		if mc == nil {
			return fmt.Errorf("nil object error: %s", content.Name)
		}
		mcList = append(mcList, mc)
	}
	// delete MC
	for _, mc := range mcList {
		if err := c.Delete(context.TODO(), mc); err != nil {
			if errors.IsNotFound(err) {
				continue
			}
			return fmt.Errorf("delete MC error: %s: %w", mc.GetName(), err)
		}
	}

	return nil
}

func CreateMC(c client.Client, s *runtime.Scheme, labels map[string]string) error {
	// e.g.) machineconfiguration.openshift.io/role: cmmd
	for _, content := range assets.GetMCs() {
		obj := decodeObject(content.Body, s)
		mc := obj.(*mcfgv1.MachineConfig)
		mc.Labels = labels

		if err := c.Create(context.TODO(), mc); err != nil {
			if errors.IsAlreadyExists(err) {
				return nil
			}
			return fmt.Errorf("create MC error: %s: %w", mc.Name, err)
		}
	}
	return nil
}

// GetDaemonsetServiceName
func GetDaemonsetServiceName() string {
	return daemonsetServiceName
}

func PatchDaemonset(c client.Client, s *runtime.Scheme) error {
	dsContents := []assets.Content{}
	for _, content := range assets.GetDaemonsets() {
		if content.Name != GetDaemonsetServiceName() {
			continue
		}
		dsContents = append(dsContents, content)
	}

	p := patcher{client: c, scheme: s}
	if err := p.patchObject(dsContents, false); err != nil {
		return err
	}

	return nil
}
