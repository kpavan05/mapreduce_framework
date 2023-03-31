#!/bin/bash

# This runs the program in KIND locally.

# Usage:
#  ./build.sh
#  ./build.sh clean
#  ./build.sh build
#  ./build.sh start
#  ./build.sh update

namespace="mr"
numprocs=$(nproc)

# Utilities
function build_image_if_missing() {
  docker_auto_sudo image inspect $1:latest > /dev/null 2> /dev/null
  if [ $? -ne 0 ]
    then build_image $1
    else echo '#' image was already built: $1
  fi
}
function build_image() {
  docker_auto_sudo build -f src/docker/Dockerfile.$1 -t $1:latest .
}
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

  if [ ! -z $2 ] && [ -z ${variable+x} ]; then
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
  _clean
  _build
  _start
}
function _clean() {
  containers=$(docker_auto_sudo container ls -aq) # get IDs
  clusters=$(kind get clusters 2> /dev/null) # get list of clusters. If none, stdOut is empty and stdErr has error message.

  if [ ! -z "$containers" ] || [ ! -z "$clusters" ]; then
      echo_ '##### Cleaning up'
    else
      echo_ '##### No cleanup needed'
      return
  fi

  if [ ! -z "$clusters" ]; then
      # We're just using the default name right now, so --all may be overkill.
      # Delete a single cluster: kind delete cluster --name kind
      kind delete clusters --all
  fi

  containers=$(docker_auto_sudo container ls -aq) # get IDs
  if [ ! -z "$containers" ]; then
      echo_ "### Removing containers"
      docker_auto_sudo container ls -a # show them in the prettier format
      docker_auto_sudo container rm -f $containers > /dev/null # $containers not in quotes so that the IDs aren't in separate lines
    else
      echo_ '### No containers to remove'
  fi

  echo_ '### Cleanup complete'
}
function _build() {
  echo_ '##### Starting build'

  # Build executables
  cmake -S . -B ./build -D USE_LOCAL=OFF -Wno-deprecated -Wno-dev # Creates/update CMake configuration files
  cmake --build build -- -j${numprocs} # Actually create the programs

  # Copy ./install.sh artifacts to build folder
  [ ! -d "./build/install_artifacts" ] && mkdir ./build/install_artifacts
  find -L /usr/local/lib/ -maxdepth 1 -type f | xargs -I {} cp {} ./build/install_artifacts/ # all files, but no folders https://unix.stackexchange.com/a/102013

  Build containers
  build_image worker
  build_image master

  # helm repo list # -o json ? only add the this if wasn't already there?
  helm repo add bitnami https://charts.bitnami.com/bitnami

  echo_ '### Build complete'
}
function _start() {
  echo_ '##### Starting up'

  # Start the cluster and apply general config
  clusters=$(kind get clusters 2> /dev/null)
  if [ -z "$clusters" ]; then
    echo_ '### Creating cluster'
    kind create cluster
    kubectl create ns $namespace # Create a Kubernetes namespace to specify where things should be.
    kubectl config set-context --current --namespace=$namespace # Set default namespace for kubectl commands
    helm install zookeeper bitnami/zookeeper -n $namespace # Use Helm to install zookeeper chart on to your cluster. This also starts the Zookeeper service
  else
    # Remove deployments and services that we created. Assumes these have the label set.
    echo_ '### Removing app=mr19 from k8s'
    kubectl -n $namespace delete services,deployments,pods --now -l app=mr19
    # Reset zookeeper data
    echo_ '### Removing zookeepeer data'
    kubectl exec -it -n $namespace service/zookeeper -- zkCli.sh deleteall /masters  > /dev/null
    kubectl exec -it -n $namespace service/zookeeper -- zkCli.sh delete /masters > /dev/null
    kubectl exec -it -n $namespace service/zookeeper -- zkCli.sh deleteall /workers > /dev/null
    kubectl exec -it -n $namespace service/zookeeper -- zkCli.sh delete d /workers > /dev/null
  fi

  # Add/update the Docker images to Kind
  echo_ '### Loading docker images'
  kind load docker-image master:latest
  kind load docker-image worker:latest

  # Deploy
  kubectl apply -f src/kubernetes/kind.yaml -n $namespace

  echo_ '### Startup complete'
}
function _update() {
  _build
  _start
}
function _yaml() {
  kubectl apply -f src/kubernetes/kind.yaml -n $namespace
}
function _local() {
  # rm -rf build
  # mkdir -p build/proto
  cmake -S . -B ./build -D USE_LOCAL=ON  -Wno-deprecated -Wno-dev

  cmake --build build -- -j${numprocs}
  # cd build
  # make -j${numprocs}
  # cd -

  cp ./build/src/master/master .
  cp ./build/src/worker/worker .
  cp ./src/python/* .

  ### Test with one of these:
  # curl -X POST http://localhost:8000/upload?container=mr19test
  # ./client.sh -l -c mr19test
}
function _local_clean() {
  rm logs/*
  rm master
  rm worker
  rm *.py
  rm leader_config.txt
  rm sh-*
  rm int_*
  rm kv-*
  rm out_*
}


# Command handling based on: https://gist.github.com/waylan/4080362
[[ $# -eq 0 ]] && command="default" || command=$1
_$command $@
if [ $? = 127 ]; then
    echo "Error: '$command' is not a known command." >&2
    exit 1
fi

