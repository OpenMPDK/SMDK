#! /usr/bin/bash

DAX_VERSION="v71.1"
REP_URL="https://github.com/OpenMPDK/SMDK/raw/refs/heads/main/lib/redhat/daxctl/${DAX_VERSION}"

curl -LO "${REP_URL}/daxctl"
curl -LO "${REP_URL}/libdaxctl.so.1"

echo "------------------------------------------------------------"
echo "File downloaded"
ls -al *dax*
echo "------------------------------------------------------------"

DAXCTL_BASE64=$(base64 -w0 ./daxctl)
LIBDAXCTL_BASE64=$(base64 -w0 ./libdaxctl.so.1)

cat >internal/assets/99-daxctl.yaml <<EOL
apiVersion: machineconfiguration.openshift.io/v1
kind: MachineConfig
metadata:
  name: 99-daxctl
spec:
  config:
    ignition:
      version: 3.4.0
    storage:
      files:
      - path: /usr/local/bin/daxctl
        mode: 493
        overwrite: true
        contents:
          source: data:text/plain;charset=utf-8;base64,$DAXCTL_BASE64
      - path: /usr/local/bin/libdaxctl.so.1
        mode: 493
        overwrite: true
        contents:
          source: data:text/plain;charset=utf-8;base64,$LIBDAXCTL_BASE64

EOL