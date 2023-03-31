#!/usr/bin/env bash

# Usage ./azLogin.sh <subscription id>

# Verify that input file was sent
if [ $# -eq 0 ]
then
  echo "No arguments supplied, need to provide subscription id"
  exit 1
fi

SUBSCRIPTION_ID=$1
az login
az account set --subscription $1
