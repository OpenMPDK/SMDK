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

package v1

import (
	metav1 "k8s.io/apimachinery/pkg/apis/meta/v1"
	"k8s.io/apimachinery/pkg/apis/meta/v1/unstructured"
)

// EDIT THIS FILE!  THIS IS SCAFFOLDING FOR YOU TO OWN!
// NOTE: json tags are required.  Any new fields you add must have json tags for the fields to be serialized.

// Specify resource allocation manually
type Allocate struct {
	NodeName string `json:"nodeName,omitempty"`

	// //+kubebuilder:validation:XValidation:message="wrong value type",rule="^([+-]?[0-9.]+)$"

	Memory string `json:"memory,omitempty"`

	// // +kubebuilder:validation:XIntOrString

	CPU string `json:"cpu,omitempty"`
}

// CMMDSpec defines the desired state of CMMD
type CMMDSpec struct {
	// INSERT ADDITIONAL SPEC FIELDS - desired state of cluster
	// Important: Run "make" to regenerate code after modifying this file

	// Whether use of CMMD
	Enable bool `json:"enable"`

	// Specify resource allocation mode
	// +kubebuilder:validation:Required
	// +kubebuilder:validation:Enum:={"auto", "manual"}
	AllocateMode string `json:"allocateMode"`

	// Specify resource allocation manually
	Allocate Allocate `json:"allocate,omitempty"`

	// Specify user resource manifest. e.g.) Pod, Deployment, Replicaset
	// +kubebuilder:pruning:PreserveUnknownFields
	// +kubebuilder:validation:EmbeddedResource
	Payload *unstructured.Unstructured `json:"payload"`
}

// CMMDStatus defines the observed state of CMMD
type CMMDStatus struct {
	// INSERT ADDITIONAL STATUS FIELD - define observed state of cluster
	// Important: Run "make" to regenerate code after modifying this file

	// Namespace
	Namespace string `json:"namespace,omitempty"`
	// Name
	Name string `json:"name,omitempty"`
	// Kind
	Kind string `json:"kind,omitempty"`
	// Enable
	Enable bool `json:"enable,omitempty"`
	// AllocateMode
	AllocateMode string `json:"allocateMode,omitempty"`
	// NodeName
	NodeName string `json:"nodeName,omitempty"`
	// Memory
	Memory string `json:"memory,omitempty"`
	// CPU
	CPU string `json:"cpu,omitempty"`
	// AmountMemory
	AmountMemory int64 `json:"amountMemory,omitempty"`
	// AmountMemoryStr
	AmountMemoryStr string `json:"amountMemoryStr,omitempty"`
}

//+kubebuilder:object:root=true
//+kubebuilder:subresource:status

// CMMD is the Schema for the cmmds API
type CMMD struct {
	metav1.TypeMeta   `json:",inline"`
	metav1.ObjectMeta `json:"metadata,omitempty"`

	Spec   CMMDSpec   `json:"spec,omitempty"`
	Status CMMDStatus `json:"status,omitempty"`
}

//+kubebuilder:object:root=true

// CMMDList contains a list of CMMD
type CMMDList struct {
	metav1.TypeMeta `json:",inline"`
	metav1.ListMeta `json:"metadata,omitempty"`
	Items           []CMMD `json:"items"`
}

func init() {
	SchemeBuilder.Register(&CMMD{}, &CMMDList{})
}
