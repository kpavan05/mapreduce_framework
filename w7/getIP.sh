#!/usr/bin/env bash

# Usage ./getIp.sh <resource group>

if [ $# -eq 0 ]
  then
  # echo "No arguments supplied, need to provide resource group"
  # exit 1
  resourcegroup="Systems_Project"
else
  # Obtain input parameter
  resourcegroup="$1"
fi

namespace="mr"

# Try to execute the command to check if the user is logged in (forward stderr to stdout)
TEST=$((az network public-ip list) 2>&1)

#Note this is not an exhaustive test, the following command could also fail if the wrong subscription is used.

# Get IP's
# IPs=$(az network public-ip list --query "[].ipAddress"  | jq --raw-output '.[]')
IPs=$(kubectl get service -l app=mr19 -n $namespace -o=custom-columns=ip:status.loadBalancer.ingress[].ip --no-headers)

# Do something with each IP
while read IP; do
  # Process IP
  echo ${IP}
done <<< "$IPs"
