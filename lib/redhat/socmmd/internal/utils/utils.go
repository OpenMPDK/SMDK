package utils

import (
	"encoding/json"
	"fmt"

	"samsung/socmmd/internal/consts"

	appsv1 "k8s.io/api/apps/v1"
	corev1 "k8s.io/api/core/v1"
	"k8s.io/apimachinery/pkg/api/resource"
	"k8s.io/apimachinery/pkg/apis/meta/v1/unstructured"
	"k8s.io/apimachinery/pkg/runtime"
	"sigs.k8s.io/controller-runtime/pkg/client"
)

type podSpecBody struct {
	Spec struct {
		Containers []struct {
			Resources struct {
				Requests struct {
					Memory string `json:"memory,omitempty"`
				} `json:"requests,omitempty"`
				Limits struct {
					Memory string `json:"memory,omitempty"`
				} `json:"limits,omitempty"`
			} `json:"resources,omitempty"`
		} `json:"containers,omitempty"`
	} `json:"spec,omitempty"`
}

type deploySpecBody struct {
	Spec struct {
		Replicas *int32 `json:"replicas,omitempty"`
		Template struct {
			podSpecBody
		} `json:"template,omitempty"`
	} `json:"spec,omitempty"`
}

// GetAmountMemory
func GetAmountMemory(kind string, obj client.Object) *resource.Quantity {
	var amount int64
	var replica = int64(1)

	bt, _ := json.Marshal(obj)

	podBody := &podSpecBody{}
	if kind == consts.ObjectKindPod {
		json.Unmarshal(bt, podBody)
	} else {
		deployBody := &deploySpecBody{}
		json.Unmarshal(bt, deployBody)
		if deployBody.Spec.Replicas != nil {
			replica = int64(*deployBody.Spec.Replicas)
		}
		podBody = &deployBody.Spec.Template.podSpecBody
	}

	for _, v := range podBody.Spec.Containers {
		memStr := v.Resources.Requests.Memory
		if memStr == "" {
			memStr = v.Resources.Limits.Memory
		}
		mem, _ := resource.ParseQuantity(memStr)
		amount += mem.Value()
	}

	amount *= replica

	return resource.NewQuantity(amount, resource.BinarySI)
}

func GetPodSpecFromPayload(payload *unstructured.Unstructured) (*corev1.PodSpec, error) {
	obj, kind, err := ConvertPayloadObject(payload)
	if err != nil {
		return nil, err
	}

	switch kind {
	case consts.ObjectKindPod:
		return &obj.(*corev1.Pod).Spec, nil
	case consts.ObjectKindDeployment:
		return &obj.(*appsv1.Deployment).Spec.Template.Spec, nil
	case consts.ObjectKindReplicaset:
		return &obj.(*appsv1.ReplicaSet).Spec.Template.Spec, nil
	}

	return nil, fmt.Errorf("not supported object kind")
}

// ConvertPayloadObject
func ConvertPayloadObject(payload *unstructured.Unstructured) (obj client.Object, kind string, err error) {
	switch kind = payload.GetKind(); kind {
	case consts.ObjectKindPod:
		obj = &corev1.Pod{}
		err = runtime.DefaultUnstructuredConverter.FromUnstructured(payload.Object, obj)
	case consts.ObjectKindDeployment:
		obj = &appsv1.Deployment{}
		err = runtime.DefaultUnstructuredConverter.FromUnstructured(payload.Object, obj)
	case consts.ObjectKindReplicaset:
		obj = &appsv1.ReplicaSet{}
		err = runtime.DefaultUnstructuredConverter.FromUnstructured(payload.Object, obj)
	default:
		err = fmt.Errorf("not supported object kind")
	}

	if err != nil {
		err = fmt.Errorf("convert to %s error: %w", kind, err)
	}

	return obj, kind, err
}
