#!/bin/bash

# Usage
#  ./azure_build.sh
#  ./azure_build.sh update
#  ./azure_build.sh clean


# This script will:
# - Build, tag, and upload Docker container images.
# - Start the AKS cluster using our k8s yaml and the helm chart for zookeeper.
# - Set the kubectl context (and unset it at cleanup)

# NOTE: This script assumes the main resource group is never deleted. We currently store some files there.


##### Notes
# Node resources are controlled by the VM size. I don't think containers need extra constraints.
# I don't think anything special needs to be done with ssh keys
# I don't think we need an app gateway
# I think building locally instead of in the cloud is perfectly fine
# This script could save an environment variable with the cluster name, enabling multiple people to deploy and test at once.

##### Debug notes
# You can check if these are enabled if you run into an issue.
#   Check:   az provider show -n Microsoft.OperationsManagement -o table; az provider show -n Microsoft.OperationalInsights -o table
#   Register:    az provider register --namespace Microsoft.OperationsManagement; az provider register --namespace Microsoft.OperationalInsights

# Example resources for a container:
        # resources:
        #   requests:
        #     cpu: 100m
        #     memory: 128Mi
        #   limits:
        #     cpu: 250m
        #     memory: 256Mi


# AKS = Azure Kubernetes Service
# RBAC = Role Based Access Control
subscription="620908e8-0b82-4c4e-8edc-a11162e979f3"
resourceGroup="Systems_Project" # This is our main resource group
AKSCluster="AKS-cluster" # This is the k8s service that manages the k8s nodes.
AKSResourceGroup="AKS_nodes" # Would be created by `az aks create`. This holds the k8s nodes. Not used because setting it would need aks-preview.
ACRRegistry="team19"
registryURL="$ACRRegistry.azurecr.io"
namespace="mr"
yaml="azure" # azure mrt19
numprocs=$(nproc)


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

function azure_account_create() {
  # This is just for notes on how the service provider was created. No need to run it.
  # This did not work even when giving the Contributor or Owner role for the resource group scope. Might work at subscription scope...
  # Maybe it needs to somehow be granted access to the node resource group that is being created automatically?
  # Deleted the account I made.
  return

  # https://docs.microsoft.com/en-us/cli/azure/authenticate-azure-cli
  # A Managed Identity is a kind of Service Principle, but can't be used on our local computers.

  # NOTE: The Azure AD API will break sometime after the end of 2022, but some official tutorials still use it.
  # Tutorial: https://docs.microsoft.com/en-us/cli/azure/create-an-azure-service-principal-azure-cli
  # Deprecation: https://docs.microsoft.com/en-us/graph/migrate-azure-ad-graph-overview

  # This might help with understanding access needed outside the resource group:
  # https://www.jamessturtevant.com/posts/Deploying-AKS-with-least-privileged-service-principal/

  # Event logs may help with debugging
  # https://portal.azure.com/#@gtvault.onmicrosoft.com/resource/subscriptions/620908e8-0b82-4c4e-8edc-a11162e979f3/eventlogs

  # Create the account
  az ad sp create-for-rbac --skip-assignment

  # Credentials for the service principle. These technically shouldn't be committed.
  appID="1d721cd6-4c5a-4097-b50c-32e3631f4942"
  displayName="azure-cli-2022-03-27-03-07-47"
  pw="UpwVZ.EV-tIuEyJgzOXR0DZdpR6TU.UrnL"
  tenant="482198bb-ae7b-4b25-8b7a-6d7f32faa083"

  # Add role
  az role assignment create \
    --assignee $appID \
    --scope "/subscriptions/620908e8-0b82-4c4e-8edc-a11162e979f3/resourceGroups/Systems_Project" \
    --role "Owner"

  az ad sp delete --id $appID
}

function compile_c() {
  echo_ "Building executables"
  cmake -S . -B ./build -D USE_LOCAL=OFF -Wno-deprecated -Wno-dev # Creates/update CMake configuration files
  cmake --build build -- -j${numprocs}

  if [ $? -ne 0 ]; then
    echo_ "Error building" "RED"
    exit 1
  fi
}

function add_helm_chart() {
  echo_ "Adding helm chart"
  # helm repo list # -o json ? only add the this if wasn't already there?
  helm repo add bitnami https://charts.bitnami.com/bitnami
}

function build_docker_images() {
  echo_ "Building docker images"
  # Copy ./install.sh artifacts to build folder
  [ ! -d "./build/install_artifacts" ] && mkdir ./build/install_artifacts
  find -L /usr/local/lib/ -maxdepth 1 -type f | xargs -I {} cp {} ./build/install_artifacts/ # all files, but no folders https://unix.stackexchange.com/a/102013

  docker_auto_sudo build -f src/docker/Dockerfile.master -t master:latest .
  docker_auto_sudo build -f src/docker/Dockerfile.worker -t worker:latest .
}

function tag_and_push_docker_images() {
  # This sends individual layers, which avoids resending the full image if nothing changed
  # Container registry info: https://docs.microsoft.com/en-us/azure/container-registry/container-registry-get-started-docker-cli?tabs=azure-cli

  # Login. This is separate from the main login, but shouldn't prompt for credentials.
  az acr login --name $ACRRegistry

  # Get the registry URL. This takes a couple seconds, so it's just hardcoded
  # registryURL=$(az acr show -n $ACRRegistry --query "loginServer" --output tsv)

  # Tag the images
  echo_ "Tagging docker images"
  docker_auto_sudo tag master:latest $registryURL/master:latest
  docker_auto_sudo tag worker:latest $registryURL/worker:latest

  # Verify they were tagged
  # docker_auto_sudo images

  # Push the images to Azure
  echo_ "Pushing docker images"
  docker_auto_sudo push $registryURL/master:latest
  docker_auto_sudo push $registryURL/worker:latest
}

function launch_cluster() {
  # AKS walkthrough (high level): https://docs.microsoft.com/en-us/azure/aks/kubernetes-walkthrough
  # AKS Tutorial (has more details): https://docs.microsoft.com/en-us/azure/aks/tutorial-kubernetes-prepare-app
  # Using multiple node pools: https://docs.microsoft.com/en-us/azure/aks/use-multiple-node-pools

  # Create AKS cluster
  #   Creates one node pool.
  #   Default container size for linux is Standard_DS2_v2. Cheaper alternates (about 1/3 to 1/2 cost): A1 D1 DS1 F2s A2 DC1s
  #   Default load balancer is standard. Even basic includes a dynamically assigned IP. Only created if there is a service.
  echo_ "Creating cluster"
  az aks create \
    --resource-group $resourceGroup \
    --name $AKSCluster \
    --attach-acr $ACRRegistry \
    --node-count 2 \
    --node-vm-size Standard_DS2_v2 \
    --enable-addons monitoring \
    --generate-ssh-keys \
    --load-balancer-sku basic
    # --service-principal $appID \ # Not using this. Just use your own account.
    # --client-secret $pw \
    # --node-resource-group $AKSResourceGroup \ # not currently supported

  if [ $retval -ne 0 ]; then
    echo_ "Cluster failed to start. Aborting azure_build.sh" "RED" >&2
    exit
  fi

  # "Downloads credentials and configures the Kubernetes CLI to use them."
  # The --overwrite-existing flag only replaces config info with the same name for the cluster.
  az aks get-credentials --resource-group $resourceGroup --name $AKSCluster --overwrite-existing

  # kubectl get nodes # just to test what happened

  # ? az aks install-cli # If needed, this should be in install.sh

  echo_ "Creating namepace and installing zookeeper"
  kubectl create ns $namespace # Create a Kubernetes namespace to specify where things should be.
  kubectl config set-context --current --namespace=$namespace
  helm install zookeeper bitnami/zookeeper -n $namespace # Use Helm to install zookeeper chart on to your cluster. This also starts the Zookeeper service

  # kubectl get service <service-name> --watch # This will show live information, including the IP address
}

function apply_yaml() {
  echo_ "Applying k8s yaml"
  # --prune=true removes anything that matches the selector and was created by apply or create but is not in the new yaml
  kubectl apply -n $namespace -f src/kubernetes/${yaml}.yaml --prune=true -l app=mr19
}

function add_yaml() {
  extra_yaml=$1

  echo_ "Applying k8s yaml: ${extra_yaml}"
  kubectl apply -n $namespace -f src/kubernetes/${extra_yaml}.yaml
}

function reset_cluster() {
  # Remove old stuff
  echo_ 'Removing app=mr19 from k8s'
  kubectl -n $namespace delete services,deployments,pods --grace-period=0 -l app=mr19 # Adding --force does not speed it up, but does add a warning
  # Note: it's probably safe to leave the service in place. I think that is what makes this call so slow, not the removal of pods and deployments.

  # Reset zookeeper data
  echo_ 'Removing zookeepeer data'
  kubectl exec -it -n $namespace service/zookeeper -- zkCli.sh deleteall /masters  > /dev/null
  kubectl exec -it -n $namespace service/zookeeper -- zkCli.sh delete /masters > /dev/null
  kubectl exec -it -n $namespace service/zookeeper -- zkCli.sh deleteall /workers > /dev/null
  kubectl exec -it -n $namespace service/zookeeper -- zkCli.sh delete d /workers > /dev/null
}

function cleanup() {
  # Turn off the cluster
  echo_ "Deleting the AKS cluster"
  az aks delete -g $resourceGroup -n $AKSCluster --no-wait -y -o table
  # This takes minutes...

  # Remove the context (cluster and context have the same name)
  echo_ "Unsetting kubectl config"
  kubectl config unset contexts.$AKSCluster
  kubectl config unset clusters.$AKSCluster
  kubectl config unset users.clusterUser_${resourceGroup}_$AKSCluster
  kubectl config unset current-context # Assumes it was the one for AKS

  # It may make sense to purge the ACR of old data. It shouldn't grow too fast unless we are changing the apt-get command.
  # https://docs.microsoft.com/en-us/azure/container-registry/container-registry-auto-purge

  # Deletes a resource group.
  # WARNING: Our main resource group includes our container registry and the gutenberg files. Do not delete it.
  # az group delete --name myResourceGroup --yes --no-wait
}

function check_cluster_status() {
  if [[ $# > 0 ]]; then
    echo_ "Getting status for $1"
    az aks show --name $1 -o table 2> /dev/null
    retval=$?
  else
    echo_ "Getting status for all clusters"
    az aks list -o table 2> /dev/null
    retval=$?
  fi

  if [ $retval -ne 0 ]; then
    echo_ "Cluster does not exist" "RED" >&2
  fi

  return $retval
}


# Utilities
function docker_auto_sudo() {
  # This avoids needing to type a password for sudo if the docker group is setup correctly.
  # https://docs.docker.com/engine/install/linux-postinstall/#manage-docker-as-a-non-root-user

  if groups | grep -q '\bdocker\b'; then
    # echo_ "in docker group"
    docker $@
  else
    # echo_ "not in docker group"
    sudo docker $@
  fi
}
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


# Commands
function _default() {
  azure_login

  compile_c
  add_helm_chart
  build_docker_images
  tag_and_push_docker_images

  launch_cluster
  apply_yaml
}
function _update() {
  # Update the service without tearing everything down and setting it back up.
  reset_cluster

  compile_c
  build_docker_images
  tag_and_push_docker_images

  apply_yaml
}
function _update_fail() {
  # Update the service without tearing everything down and setting it back up. Setup failing pods first.
  reset_cluster

  compile_c
  build_docker_images
  tag_and_push_docker_images

  add_yaml 'fail'
  sleep 3
  add_yaml ${yaml}
}
function _clean() {
  cleanup
}
function _reset() {
  reset_cluster
}
function _compile() {
  compile_c
}
function _yaml() {
  # This may behave somewhat inconsistently. So if a change fixes something and changing it back doesn't break it, try cleaning up and restarting the cluster a few times to make sure it works. Alternatively, the update command in this script might do enough cleanup.
  apply_yaml
}
function _yaml_hard() {
  reset_cluster
  apply_yaml
}
function _yaml_fail() {
  reset_cluster

  add_yaml 'fail'
  sleep 3
  add_yaml ${yaml}
}
function _status() {
  check_cluster_status $AKSCluster
  return $?
}
function _status_all() {
  check_cluster_status
  return $?
}


# Command handling based on: https://gist.github.com/waylan/4080362
[[ $# -eq 0 ]] && command="default" || command=$1
_$command $@
retval=$?
if [ $retval = 127 ]; then
    echo_ "Error: '$command' is not a known command." "RED" >&2
    exit 1
fi
exit $retval
