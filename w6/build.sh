# Script to build your protobuf, c++ binaries, and docker images here

function build_image_if_missing() {
  sudo docker image inspect $1:latest > /dev/null 2> /dev/null
  if [ $? -ne 0 ]
    then build_image $1
    else echo '#' image was already built: $1
  fi
}
function build_image() {
  sudo docker build -f src/docker/Dockerfile.$1 -t $1:latest .
}
function cleanup() {
  containers=$(sudo docker container ls -aq) # get IDs
  clusters=$(kind get clusters 2> /dev/null) # get list of clusters. If none, stdOut is empty and stdErr has error message.

  if [ ! -z "$containers" ] || [ ! -z "$clusters" ]
    then
      echo_ '##### Cleaning up' 'BLUE'
    else
      echo_ '##### No cleanup needed' 'BLUE'
      return
  fi

  if [ ! -z "$clusters" ]
    then
      # We're just using the default name right now, so --all may be overkill.
      # Delete a single cluster: kind delete cluster --name kind
      kind delete clusters --all
  fi

  containers=$(sudo docker container ls -aq) # get IDs
  if [ ! -z "$containers" ]
    then
      echo_ "### Removing containers" 'BLUE'
      sudo docker container ls -a # show them in the prettier format
      sudo docker container rm -f $containers > /dev/null # $containers not in quotes so that the IDs aren't in separate lines
    else
      echo_ '### No containers to remove' 'BLUE'
  fi

  echo_ '### Cleanup complete' 'BLUE'
}
function echo_() {
  # echo <string> <style name>
  # based on https://stackoverflow.com/a/287944 which cites Blender
  output=$1

  if [[ $BASH_VERSION < '4' ]]
    then
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

  if [ ! -z $2 ] && [ -z ${variable+x} ]
    then
    printf "${style_map[$2]}"
    printf "$output"
    printf "${style_map['ENDC']}\n"
    # output="${style_map[$2]}$output${style_map['ENDC']}\n"
  else
    echo $output
  fi
}

function build() {
  echo_ '##### Starting build' 'BLUE'

  # Build executables
  [ ! -d "./build" ] && cmake -H. -Bbuild # Creates CMake configuration files if build directory doesn't exist yet
  cmake --build build -- -j3 # Actually create the programs

  # Copy ./install.sh artifacts to build folder
  [ ! -d "./build/install_artifacts" ] && mkdir ./build/install_artifacts
  find /usr/local/lib/ -maxdepth 1 -type f | xargs -I {} cp {} ./build/install_artifacts/ # all files, but no folders https://unix.stackexchange.com/a/102013

  # Build containers
  build_image worker
  build_image master

  # helm repo list # -o json ? only add the this if wasn't already there?
  helm repo add bitnami https://charts.bitnami.com/bitnami

  echo_ '### Build complete' 'BLUE'
}

function start() {
  echo_ '##### Starting up' 'BLUE'

  namespace="mr"

  # Start the cluster and apply general config
  clusters=$(kind get clusters 2> /dev/null)
  if [ -z "$clusters" ]
    then
    echo_ '### Creating cluster' 'BLUE'
    kind create cluster
    kubectl create ns $namespace # Create a Kubernetes namespace to specify where things should be.
    helm install zookeeper bitnami/zookeeper -n $namespace # Use Helm to install zookeeper chart on to your cluster. This also starts the Zookeeper service
  else
    # Remove deployments and services that we created. Assumes these have the label set.
    echo_ '### Removing app=mr19 from k8s' 'BLUE'
    kubectl -n $namespace delete services,deployments,pods --now=true -l app=mr19
    # Reset zookeeper data
    echo_ '### Removing zookeepeer data' 'BLUE'
    kubectl exec -it -n $namespace service/zookeeper -- zkCli.sh deleteall /masters  > /dev/null
    kubectl exec -it -n $namespace service/zookeeper -- zkCli.sh delete /masters > /dev/null
    kubectl exec -it -n $namespace service/zookeeper -- zkCli.sh deleteall /workers > /dev/null
    kubectl exec -it -n $namespace service/zookeeper -- zkCli.sh delete d /workers > /dev/null
  fi

  # Add/update the Docker images to Kind
  echo_ '### Loading docker images' 'BLUE'
  kind load docker-image master:latest
  kind load docker-image worker:latest

  # Deploy
  kubectl apply -f src/kubernetes/config.yaml -n $namespace

  echo_ '### Startup complete' 'BLUE'
}

# echo_ 'test' 'BLUE'
# exit()

if [ -z ${1// } ] # if $1 has zero length after spaces are removed
  then
  cleanup
  build
  start
elif [ $1 = 'clean' ]
  then
  cleanup
elif [ $1 = 'build' ]
  then
  build
elif [ $1 = 'start' ]
  then
  start
elif [ $1 = 'update' ] # this isn't well tested. I don't know how well it will switch out old image
  then
  build
  start
fi

