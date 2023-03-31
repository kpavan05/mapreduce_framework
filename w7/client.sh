#!/bin/bash

# Usage:
# ./client.sh -c <container> -f file1.txt,file2.txt -m 5 -r 2
# All arguments are optional.
# If files are specified, must also specify container.
# If no files are specified, assumes the container already exists.


# Azure
subscription='620908e8-0b82-4c4e-8edc-a11162e979f3'
resourceGroup='Systems_Project'
defaultContainer='mr19container' # Used if no container or files are specified.
container='' # --container / -c
storageAccount='mr19'
storageKey='' # fetched automatically

# AKS / Kubernetes
namespace="mr"
app='mr19'
ip='' # fetched automatically

# Our app
files='' # --files / -f. comma separated
folder='' # --folder / -F. not supported, but could be if we want
m=3 # --map-tasks / -m
r=3 # --reduce-tasks / -r
map='map' # --map
reduce='reduce' # --reduce
local_=0 # --local / -l

# Run script
function parse_arguments() {
  # Based on: https://stackoverflow.com/a/29754866

  # More safety, by turning some bugs into errors.
  # Without `errexit` you don’t need ! and can replace
  # ${PIPESTATUS[0]} with a simple $?, but I prefer safety.
  set -o errexit -o pipefail -o noclobber -o nounset

  # -allow a command to fail with !’s side effect on errexit
  # -use return value from ${PIPESTATUS[0]}, because ! hosed $?
  ! getopt --test > /dev/null
  if [[ ${PIPESTATUS[0]} -ne 4 ]]; then
      echo 'I’m sorry, `getopt --test` failed in this environment.'
      exit 1
  fi

  # option --output/-o requires 1 argument
  LONGOPTS=container:,map-tasks:,reduce-tasks:,files:,folder:,map:,reduce:,local
  OPTIONS=c:m:r:f:F:l

  # -regarding ! and PIPESTATUS see above
  # -temporarily store output to be able to check for errors
  # -activate quoting/enhanced mode (e.g. by writing out “--options”)
  # -pass arguments only via   -- "$@"   to separate them correctly
  ! PARSED=$(getopt --options=$OPTIONS --longoptions=$LONGOPTS --name "$0" -- "$@")
  if [[ ${PIPESTATUS[0]} -ne 0 ]]; then
      # e.g. return value is 1
      #  then getopt has complained about wrong arguments to stdout
      exit 2
  fi
  # read getopt’s output this way to handle the quoting right:
  eval set -- "$PARSED"

  d=n f=n v=n outFile=-
  # now enjoy the options in order and nicely split until we see --
  while true; do
      case "$1" in
          -c|--container)
              container="$2"
              shift 2
              ;;
          -m|--map-tasks)
              m="$2"
              shift 2
              ;;
          -r|--reduce-tasks)
              r="$2"
              shift 2
              ;;
          -f|--files)
              files="$2"
              shift 2
              ;;
          -F|--folder)
              folder="$2"
              shift 2
              ;;
          --map)
              map="$2"
              shift 2
              ;;
          --reduce)
              reduce="$2"
              shift 2
              ;;
          -l|--local)
              local_=1
              shift 1
              ;;
          --)
              shift
              break
              ;;
          *)
              echo "Programming error"
              exit 3
              ;;
      esac
  done

  if [ -z "$container" ] && ([ -n "$files" ] || [ -n "$folder" ]); then
    echo_ "Must specify container when files or folder are specified" "RED" >&2
    exit 1
  fi
  if [ -z "$container" ] && [ -z "$files" ] && [ -z "$folder" ]; then
    container=$defaultContainer
  fi
  echo_ "Arguments   container:$container m:$m r:$r files:$files folder:$folder map:$map reduce:$reduce local:$local_" "CYAN"
}
function azure_login() {
  az account show > /dev/null 2> /dev/null
  if [ $? -ne 0 ]; then
    echo_ "Logging in as user"
    az login
  else
    echo_ "Already logged in"
  fi

  # echo_ "Logging in as service principal". # It appears a service principal cannot create an aks cluster
  # az login --service-principal -u $appID -p $pw --tenant $tenant

  # Set some variables
  az account set --subscription $subscription
  az configure --defaults group=$resourceGroup

  # az logout
}
function find_storage_account_key() {
  storageKey=$(az storage account keys list --resource-group $resourceGroup --account-name $storageAccount -o tsv --query [0].value)
  if [ $? -ne 0 ]; then
    echo_ "Failed to fetch account storage key" "RED" >&2
    exit 1
  fi
}
function upload_files() {
  echo_ "Creating container $resourceGroup.$storageAccount.$container"

  az storage container create --resource-group $resourceGroup --account-name $storageAccount --name $container --fail-on-exist --account-key $storageKey
  if [ $? -ne 0 ]; then
    echo_ "Failed to create container" "RED" >&2
    exit 1
  fi

  if [ -n "$files" ]; then
    filelist=$(echo $files | tr "," "\n")
    for file in $filelist
    do
        echo_ "Uploading file: $file"

        az storage blob upload \
          --content-encoding="UTF-8" \
          --type block \
          --container-name $container \
          --file $file \
          --account-name $storageAccount \
          --account-key $storageKey
    done
  elif [ -n "$folder" ]; then
    echo_ "Uploading folder: $folder"
    az storage blob upload-batch \
          --content-encoding="UTF-8" \
          --type block \
          --destination $container \
          --source $folder \
          --account-name $storageAccount \
          --account-key $storageKey \
          --pattern '*.txt'
  fi
}
function empty_container() {
  exists=$(az storage container exists --account-name $storageAccount --name $1 --account-key $storageKey -o tsv --query exists)
  # echo_ "$resourceGroup.$storageAccount.$1 exists: $exists"
  if [ "$exists" = "false" ]; then
    return
  fi

  echo_ "Deleting all files from container $resourceGroup.$storageAccount.$1" "YELLOW"
  # # https://docs.microsoft.com/en-us/answers/questions/480591/deleting-multiple-blobs-in-azure-web-console.html
  az storage blob delete-batch --account-name $storageAccount --source $1 --account-key $storageKey --pattern '*'
}
function find_ip() {
  if [[ $local_ = 1 ]]; then
    ip='localhost'
  else
    IPs=$(kubectl get service -l app=${app} -n $namespace -o=custom-columns=ip:status.loadBalancer.ingress[].ip --no-headers)
    while read IP; do
      ip=${IP} # should only be one
    done <<< "$IPs"
  fi

  if [ "$ip" = "<none>" ] || [ -z "$ip" ]; then
    echo_ "No IP" 'RED'
    exit
  fi
  echo_ "Using IP: ${ip}"
}
function run_job() {
  startTime=$SECONDS

  start_job
  echo_ 'Job submitted. Pinging for completion...' 'BOLD'
  until is_job_complete
  do
    sleep 0.1
  done

  duration=$(( SECONDS - startTime ))
  echo_ "Job took approximately $duration seconds"
}
function start_job() {
  # Without quotes the &'s make this run in the background
  curl -X POST "http://${ip}:8000/upload?container=${container}&m=${m}&r=${r}&map=${map}&reduce=${reduce}" > /dev/null 2> /dev/null &
}
function is_job_complete() {
  # /dev/null because we don't care if the container doesn't exist
  count=$(az storage blob list --container-name "${container}out" --account-name ${storageAccount} --account-key $storageKey --query "length(@)" 2> /dev/null)
  # echo $count

  [[ "$count" -ge "$r" ]]
  return
}

# Utilities
function echo_() {
  # echo <string> <style name>
  # based on https://stackoverflow.com/a/287944 which cites Blender
  output=$1

  if [[ $BASH_VERSION < '4' ]]; then
    echo $output
    return
  fi

  declare -A style_map # Bash arrays: https://stackoverflow.com/a/691157
  style_map['HEADER']="\033[95m"
  style_map['BLUE']="\033[94m"
  style_map['CYAN']='\033[96m'
  style_map['GREEN']='\033[92m'
  style_map['YELLOW']='\033[93m'
  style_map['RED']='\033[91m'
  style_map['ENDC']='\033[0m'
  style_map['BOLD']='\033[1m'
  style_map['UNDERLINE']='\033[4m'

  if [[ $# > 1 ]] && [ -z ${variable+x} ]; then
    style=$2
  else
    style="BLUE"
  fi

  printf "${style_map[$style]}"
  printf "$output"
  printf "${style_map['ENDC']}\n"
}

parse_arguments $@
azure_login
find_storage_account_key
if [ -n "$files" ] || [ -n "$folder" ]; then
  upload_files
fi
empty_container "${container}int"
empty_container "${container}out"
find_ip
run_job



