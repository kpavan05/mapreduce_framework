#!/usr/bin/env bash

# Usage ./getIp.sh <resource group>

# Verify that input file was sent
if [ $# -eq 0 ]
then
  echo "No arguments supplied, need to provide resource group"
  exit 1
fi

# Obtain input parameter
resourcegroup="$1"

# Try to execute the command to check if the user is logged in (forward stderr to stdout)
TEST=$((az network public-ip list) 2>&1)

#Note this is not an exhaustive test, the following command could also fail if the wrong subscription is used.

# Get IP's
az network public-ip list --query "[].ipAddress"  | jq --raw-output '.[]' > /tmp/ip.txt

# Do something with each IP
while read IP; do
  # Process IP
  echo ${IP}
done < /tmp/ip.txt
