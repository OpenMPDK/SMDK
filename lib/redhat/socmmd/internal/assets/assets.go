package assets

import (
	"bytes"
	"embed"
	"fmt"

	"samsung/socmmd/internal/consts"
	"samsung/socmmd/internal/packageinfo"

	consolev1 "github.com/openshift/api/console/v1"
	operv1 "github.com/openshift/api/operator/v1"
	mcfgv1 "github.com/openshift/machine-config-operator/pkg/apis/machineconfiguration.openshift.io/v1"
	"gopkg.in/yaml.v3"
	appsv1 "k8s.io/api/apps/v1"
	corev1 "k8s.io/api/core/v1"
	rbacv1 "k8s.io/api/rbac/v1"

	"k8s.io/apimachinery/pkg/runtime"
	"k8s.io/apimachinery/pkg/runtime/serializer"
)

var (
	//go:embed *.yaml
	yamlFS            embed.FS
	mcList            []Content
	mcNameList        []string
	mcContentPathList []string
	dsList            []Content
	svcList           []Content
	saList            []Content
	croleList         []Content
	crolebindingList  []Content
	cmList            []Content
	deployList        []Content
	consoleList       []Content
	consolePluginList []Content
)

type Content struct {
	Namespace string
	Name      string
	Kind      string
	Body      []byte
}

type basicObject struct {
	Kind     string `yaml:"kind"`
	Metadata struct {
		Namespace string            `yaml:"namespace"`
		Name      string            `yaml:"name"`
		Labels    map[string]string `yaml:"labels"`
	} `yaml:"metadata"`
}

type machineConfig struct {
	Metadata struct {
		Name string `yaml:"name"`
	} `yaml:"metadata"`
	Spec struct {
		Config struct {
			Storage struct {
				Files []struct {
					Path string `yaml:"path"`
				} `yaml:"files"`
			} `yaml:"storage"`
		} `yaml:"config"`
	} `yaml:"spec"`
}

func Init(scheme *runtime.Scheme) error {
	entries, err := yamlFS.ReadDir(".")
	if err != nil {
		return fmt.Errorf("read fs error: %w", err)
	}

	for _, entry := range entries {
		if entry.IsDir() {
			continue
		}

		body, err := yamlFS.ReadFile(entry.Name())
		if err != nil {
			return fmt.Errorf("read file error: [%s] %w", entry.Name(), err)
		}

		bodySlice := bytes.Split(body, []byte("---\n"))

		for _, b := range bodySlice {
			if len(bytes.TrimSpace(b)) == 0 {
				continue
			}

			obj := &basicObject{}
			err = yaml.Unmarshal(b, obj)
			if err != nil {
				return fmt.Errorf("unmarshal error: [%s] %w", entry.Name(), err)
			}

			err = addContent(obj, b, scheme)
			if err != nil {
				return fmt.Errorf("add content error: [%s] %w", entry.Name(), err)
			}
		}

	}
	return nil
}

// addContent
func addContent(obj *basicObject, body []byte, scheme *runtime.Scheme) error {
	var err error
	c := Content{
		// Namespace: obj.Metadata.Namespace,
		Namespace: packageinfo.GetNamespace(),
		Name:      obj.Metadata.Name,
		Kind:      obj.Kind,
		Body:      body,
	}
	if err := checkTypeCast(c, scheme); err != nil {
		return err
	}

	switch obj.Kind {
	case consts.ObjectKindDeployment:
		deployList = append(deployList, c)
	case consts.ObjectKindDaemonset:
		dsList = append(dsList, c)
	case consts.ObjectKindService:
		svcList = append(svcList, c)
	case consts.ObjectKindServiceAccount:
		saList = append(saList, c)
	case consts.ObjectKindConfigMap:
		cmList = append(cmList, c)
	case consts.ObjectKindClusterRole:
		croleList = append(croleList, c)
	case consts.ObjectKindClusterRoleBinding:
		crolebindingList = append(crolebindingList, c)
	case consts.ObjectKindConsole:
		consoleList = append(consoleList, c)
	case consts.ObjectKindConsolePlugin:
		consolePluginList = append(consolePluginList, c)
	case consts.ObjectKindMC:
		if isRequiredMC(c) {
			mcList = append(mcList, c)
			err = addOptionalInfo(body)
		}
	default:
		err = fmt.Errorf("not supported object kind: %s", obj.Kind)
	}
	return err
}

// checkTypeCast to check content to prevent type cast error in references.
func checkTypeCast(content Content, scheme *runtime.Scheme) error {
	decode := serializer.NewCodecFactory(scheme).UniversalDeserializer().Decode
	obj, _, err := decode(content.Body, nil, nil)
	if err != nil {
		return fmt.Errorf("decode error: [%s][%s} %w", content.Kind, content.Name, err)
	}

	ok := false
	switch content.Kind {
	case consts.ObjectKindDeployment:
		_, ok = obj.(*appsv1.Deployment)
	case consts.ObjectKindDaemonset:
		_, ok = obj.(*appsv1.DaemonSet)
	case consts.ObjectKindService:
		_, ok = obj.(*corev1.Service)
	case consts.ObjectKindServiceAccount:
		_, ok = obj.(*corev1.ServiceAccount)
	case consts.ObjectKindConfigMap:
		_, ok = obj.(*corev1.ConfigMap)
	case consts.ObjectKindClusterRole:
		_, ok = obj.(*rbacv1.ClusterRole)
	case consts.ObjectKindClusterRoleBinding:
		_, ok = obj.(*rbacv1.ClusterRoleBinding)
	case consts.ObjectKindConsole:
		_, ok = obj.(*operv1.Console)
	case consts.ObjectKindConsolePlugin:
		_, ok = obj.(*consolev1.ConsolePlugin)
	case consts.ObjectKindMC:
		_, ok = obj.(*mcfgv1.MachineConfig)
	}

	if !ok {
		return fmt.Errorf("type cast error: [%s][%s]", content.Kind, content.Name)
	}

	return nil
}

// addOptionalInfo
func addOptionalInfo(body []byte) error {
	mc := &machineConfig{}
	err := yaml.Unmarshal(body, mc)
	if err != nil {
		return fmt.Errorf("additional info unmarshal error: %w", err)
	}

	for _, f := range mc.Spec.Config.Storage.Files {
		mcContentPathList = append(mcContentPathList, f.Path)
	}
	mcNameList = append(mcNameList, mc.Metadata.Name)

	return nil
}

func GetDaemonsets() []Content {
	return dsList
}

func GetServices() []Content {
	return svcList
}

func GetServicesAccount() []Content {
	return saList
}

func GetConfigMaps() []Content {
	return cmList
}

func GetClusterRole() []Content {
	return croleList
}

func GetClusterRoleBinding() []Content {
	return crolebindingList
}

func GetConsole() []Content {
	return consoleList
}

func GetConsolePlugin() []Content {
	return consolePluginList
}
func GetDeployment() []Content {
	return deployList
}

func GetMCs() []Content {
	return mcList
}

func GetMCNames() []string {
	return mcNameList
}

func GetMCContentPaths() []string {
	return mcContentPathList
}

func isRequiredMC(c Content) bool {
	if c.Name == "99-daxdev-reconfigure" {
		// rhel 9.2 버전에서는 cmmd 장치 활성화를 위한 mc 필요
		if packageinfo.GetRhelVersion() >= 9.4 {
			return false
		}
	}
	return true
}
