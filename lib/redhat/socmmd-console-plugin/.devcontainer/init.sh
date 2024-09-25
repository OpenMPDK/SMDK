#!/bin/bash

if [[ ! -f .devcontainer/dev.env ]]
then
    cat << EOF
    env file 'dev.env' does not exist in .devcontainer, please create it and add the the correct values for your cluster.
    OC_PLUGIN_NAME=my-plugin
    OC_URL=https://api.example.com:6443
    OC_USER=kubeadmin
    OC_PASS=<password>
EOF
    exit 2
else
    echo 'found 'dev.env' in .devcontainer'
fi

# if one of the variables are missing, abort the build.
success=1
! grep -q OC_PLUGIN_NAME= ".devcontainer/dev.env" && success=0
! grep -q OC_URL= ".devcontainer/dev.env" && success=0
! grep -q OC_USER= ".devcontainer/dev.env" && success=0
! grep -q OC_PASS= ".devcontainer/dev.env" && success=0

if ((success)); then
    echo 'dev.env is formatted correctly, proceeding.'
else
    cat << EOF
    dev.env is not formatted correctly, please add the the correct values for your cluster.
    OC_PLUGIN_NAME=my-plugin
    OC_URL=https://api.example.com:6443
    OC_USER=kubeadmin
    OC_PASS=<password>
EOF
exit 2
fi
