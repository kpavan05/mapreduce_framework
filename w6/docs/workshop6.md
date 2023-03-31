# Workshop 6  

[Return to overall README](../README.md)  
To run workshop 6 as it was at the time of grading, checkout commit 3c0b1f144dabfb58925fe45aeee2cc0820e05fd2  

## Demo  
1. Create 2 masters simultaneously and 1 worker  
    - The leader will do an RPC call to the worker with its IP address as the input  
    - `./build.sh`  
    - `kubectl get all -n mr -o wide`: see what is running
    - `kubectl -n mr logs pod/<pod-id>`: see logs from the pod
2. Kill the current leader  
    - `kubectl -n mr delete pod/<pod-id>`  
    - The other master will become leader  
    - The new leader will do an RPC call to the worker with its IP address as the input  
    - k8s will start spinning up a replacement pod for the dead leader  
4. Kill the current leader (again)  
    - The third master will become the leader  
    - The new leader will do an RPC call to the worker with its IP address as the input  
    - k8s will start spinning up a replacement pod for the dead leader  

## Work Distribution  
- Local master and worker: Pavan  
- Dockerfiles & build script: Jonathan and Pavan  
- Initial k8s YAML: Pavan  
- Getting zookeeper to connect in k8s: Jonathan  
- Improve leader selection: Jonathan and Pavan  
- Glog setup and debugging: Pavan and Jonathan  
- Various other debugging: Jonathan and Pavan  

&nbsp;  
&nbsp;  
&nbsp;  
&nbsp;  

Original README contents for this repository below:  
---
# MapReduce C++ Base Repository
## Installation
Run the `install.sh` script

The install script will do the following:
1. Install kubectl
2. Install protobuf
3. Install Kind
4. Install Helm
5. Install gRPC
6. Set up conservator

Let us know if you have any issues.

## Directory Structure
CMakeLists.txt will help you build your code. We recommend you read into cmake to understand how it works and how to use it to build your services.

The starter code for your master and worker files are in the src/ directory, in the master and worker cc files. 

Happy Coding :)