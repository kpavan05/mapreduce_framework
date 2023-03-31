## Build project  
- Run once to setup the development/deployment machine: `./install.sh`  
- Run locally
  - `./build.sh local`: compile code  
    - This will effectively add `#define LOCAL 1` to master.cc and worker.cc  
  - You must manually run as many `./worker` and `./master` instances as you'd like.  
    - *Note: we could have a script that takes argments for how many of each to run in the background and pipes their output to a file, then can later locate and terminate them.*  
- Run locally in KIND: `./build.sh`  
  - If you get `kind: command not found`, add `export PATH="$HOME/go/bin:$PATH"` to the end of your `~/.bashrc` file, then start a new shell  
  - `./build.sh clean` or `kind delete clusters --all`: turn off the cluster  
  - `./build.sh build`: Build code and containers  
  - `./build.sh start`: Deploy or redeploy  
  - `./build.sh update`: Build and deploy/redeploy without deleting the cluster  
  - This uses `kind.yaml`, which may be out of date, as testing on this platform stopped during workshop 8  
- Run in azure (w7+): `./azure_build`  
  - You may need to manually set up the Azure Container Registry and Azure Storage Account, then store their names and other details in the appropriate fields at the top of the script.  
  - Supports subcommands including `clean` and `update`. See file for full list.  
  - Must manually use `clean` subcommand when done with the cluster.  
    - Some resources like the network watcher that are automatically created on cluster launch are not cleaned up by this.  
  - This uses `azure.yaml` (Jonathan) or `mrt19.yaml` (Pavan)  
- Start job: `./client.sh --container <containerName>`  
  - All options have short versions, such as `-c` for `--container`  
  - Use `--local` or `-l` to connect to localhost instead of a k8s cluster.  
  - Can specify number of map (`-m`) and reduce (`-r`) tasks.  
  - Can create a container by specifying either a folder or a list of files.  
    - `--folder` or `-F`  
    - `--files` or `-f` (comma-delimited)  
  - *Note: Workshops 6 and 7 don't use this as they start their jobs automatically.*

---
# Helpful *kubectl* commands  
- `kubectl get all -n mr`  
  - add `--watch` for live information (single resource type only)  
- `kubectl -n mr logs pod/<pod-id>`  
- `kubectl -n mr delete pod/<pod-id>`  
- `kubectl exec -it -n mr pod/<pod-id> -- bash`: Start a terminal on a pod  
- `k -n mr exec -it <pod-id> -- tail -n +0 -F /srv/project/logs/master.INFO`: live tail with full history  
  - or: `k -n mr exec -it <pod-id> -- ./tail`  
- `kubectl get services -n mr`  
- `kubectl config set-context --current --namespace=mr`: set the default namespace so you don't need to type it every time  
- `kubectl scale --replicas=5 deployment/worker`: scale pods  
- `kubectl autoscale deployment worker --cpu-percent=50 --min=3 --max=10`: autoscale pods  
- `kubectl logs --previous <pod-id>`: See log from the previous start for the pod  

---
# Helpful *zookeeper* commands  
- `zktreeutil -D -z zookeeper.mr.svc.cluster.local:2181`  
  - *Note: I've had trouble getting this to work, even from a master or worker pod*  
- `kubectl exec -it -n mr service/zookeeper -- zkCli.sh`: enter the zk pod in zk command line  
  - `ls -R /`: show tree  
  - `get /masters`: get the data stored in a node  
  - `quit`: exit the cli (does not quit zookeeper)  

---
# Helpful *azure* commands  
- `azure network public-ip list [options] [resource-group]`  
- `az group list`: see all our resource groups  
- `az aks show --resource-group Systems_Project --name our-AKS-cluster --output table`: show cluster status  
- `az aks scale --resource-group Systems_Project --name AKS-cluster --node-count 3`: scale nodes  

---
# Helpful *docker* commands  
- `docker image history <image-name>`: Lists the layers and their sizes  

---
# Miscellaneous links  
- [kubectl cheatsheet](https://kubernetes.io/docs/reference/kubectl/cheatsheet/) *includes autocomplete info*  
  - [Access multiple clusters](https://kubernetes.io/docs/tasks/access-application-cluster/configure-access-multiple-clusters/) - [Services](https://kubernetes.io/docs/concepts/services-networking/service/#type-nodeport) - [Readiness probes](https://loft.sh/blog/kubernetes-readiness-probes-examples-and-common-pitfalls/)  
- zookeeper [recipes](https://zookeeper.apache.org/doc/current/recipes.html) - [programmers](https://zookeeper.apache.org/doc/r3.3.3/zookeeperProgrammers.html)  
  - [conservator repo](https://github.gatech.edu/cs8803-SIC/conservator)  
- kind [quick start](https://kind.sigs.k8s.io/docs/user/quick-start/) - [repo](https://github.com/kubernetes-sigs/kind)  
- [Helm chart for zookeeper](https://github.com/bitnami/charts/tree/master/bitnami/zookeeper)  
- [Inspect k8s network (DigitalOcean)](https://www.digitalocean.com/community/tutorials/how-to-inspect-kubernetes-networking)  
- [gRPC intro](https://grpc.io/docs/what-is-grpc/introduction/)  
- [glog user guide](https://github.com/google/glog#user-guide)  
- Azure [install CLI](https://docs.microsoft.com/en-us/cli/azure/install-azure-cli)  
  - Blobs [SDK](https://docs.microsoft.com/en-us/dotnet/api/azure.storage.blobs.specialized.blockblobclient?view=azure-dotnet) - [quickstart](https://docs.microsoft.com/en-us/azure/storage/blobs/quickstart-blobs-c-plus-plus?tabs=environment-variable-windows) - [Other blobs links](https://piazza.com/class/ky5w36ny9jq5yb?cid=185) - [block, blob, and append](https://docs.microsoft.com/en-us/rest/api/storageservices/understanding-block-blobs--append-blobs--and-page-blobs)  
  - [k8s (AKS)](https://docs.microsoft.com/en-us/azure/aks/kubernetes-walkthrough)  
  - [container registry (ACR)](https://docs.microsoft.com/en-us/azure/container-registry/container-registry-get-started-docker-cli?tabs=azure-cli)  
  - HDInsight (workshop 5) [Create linux clusters](https://docs.microsoft.com/en-us/azure/hdinsight/hdinsight-hadoop-create-linux-clusters-portal) - [upload data](https://azure.microsoft.com/en-us/documentation/articles/hdinsight-upload-data/) - [run examples](https://docs.microsoft.com/en-us/azure/hdinsight/hadoop/apache-hadoop-run-samples-linux) - [with Python](https://www.michael-noll.com/tutorials/writing-an-hadoop-mapreduce-program-in-python/) - [reduce joins](http://codingjunkie.net/mapreduce-reduce-joins/)  
  - [Investigating performance issues](https://github.com/Azure/AKS/issues/1373)  
- [C++ netlib](https://cpp-netlib.org/)  
- Python [extending python with C](https://docs.python.org/2/extending/extending.html#calling-python-functions-from-c)  
- Bash - [autocompletion](https://askubuntu.com/questions/68175/how-to-create-script-with-auto-complete)  

---
# Background info  
- [The MapReduce paper](https://static.googleusercontent.com/media/research.google.com/en//archive/mapreduce-osdi04.pdf)  
- [The Gutenberg dataset](https://drive.google.com/file/d/1mBC8r-tY9NJFs6cUN_k03LjwrU86Z7S8/view)  
  - These files are not all ASCII but they can all be treated as containing single-byte characters.
  - Only one file has multi-byte characters and it will be excluded from our sample. For our basic purposes, it wouldn't matter anyways in most cases.  
- [Movie Lens dataset](http://grouplens.org/datasets/movielens/) (workshop 5)  
- We are not using GFS (Google File System)  

---
# Azure usage
- Shut down VMs when done with a session.  
  - Auto-shutdown can be configured  
- More credits may be doled out as needed, but we should try not to burn through them.  
- We do not need the premium SSD storage, but a standard SSD may provide good value  
- [Education hub](https://portal.azure.com/#blade/Microsoft_Azure_Education/EducationMenuBlade/overview)  
