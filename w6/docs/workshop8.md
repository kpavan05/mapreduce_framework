# Workshop 8  

We also have a [REPORT](../../w7/REPORT_W8.md) for workshop 8.  
The code for this workshop is a continuation of the code for [workshop 7](../../w7/)  
Workshop 8 reference commit: `ebd57d1ba6ccf7816c6df2c855a770a114fc73e9`  
[Return to module README](../README.md)  

## Demo  
1. Deploy on Azure Kubernetes Service  
    - `./azure_build`: Launch the cluster from scratch  
    - `./azure_build update`: Remove the app service and deployments and clean the zookeeper database before redeploying on an existing cluster  
2. Configure kubectl to point to Azure cluster  
    - This is done automatically by the build script: `az aks get-credentials --resource-group Systems_Project --name AKS-cluster`  
    - You can confirm this with: `kubectl config view`  
3. Demonstrate scaling the number of master and worker nodes  
    - `kubectl scale --replicas=5 deployment/worker`  
    - `kubectl scale --replicas=3 deployment/master`  
4. Submit a job via HTTP request to the cluster  
    - URL: `curl -X POST http://<ip>:8000/upload?container=mr19container`  
    - Script: `./client.sh -c test1 -F src/files`  
    - Script: `./client.sh -c test2 -f src/python/map.py,src/python/reduce.py`  
5. Show the output of the MapReduce job  
    - The output files are in a container named `${inputContainer}out`  

## Work Distribution  
- Run python from c: Pavan  
- Load docker containers into Azure: Jonathan  
- Run k8s using AKS: Jonathan  
- Update install script: Jonathan  
- HTTP server running locally: Pavan  
- HTTP server running in cloud: Jonathan  
- Send reduce jobs to workers: Pavan  
- Initial map and reduce python: Pavan  
- Debug python: Jonathan and Pavan  
- Client script: Jonathan  
- Readiness probe: Pavan  
- Various other debugging: Pavan and Jonathan  
- README: Jonathan  
- REPORT: Pavan and Jonathan  


## Prerequisites  
- Run `install.sh` in the w6 directory to install all libraries needed for this workshop sequence  

&nbsp;  
&nbsp;  
&nbsp;  
&nbsp;  

Original README contents for workshop 8 are below:  
---
# Workshop 8

- Before running ./getIp.sh, you always need to run ./azLogin.sh to log-in to azure (once per new terminal).
