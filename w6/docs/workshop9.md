# Workshop 9  

The code for this workshop is a continuation of the code for [workshop 7](../../w7/)  
[Return to overall README](../README.md)  

---
## Demo  
1. Get the master and worker fail parameters from the TA  
2. Start the system with those parameters  
    - Edit env values in `src/kubernetes/fail.yaml`  
    - `./azure_build reset`: optional. Cleans up k8s and zk so subsequent fail goes faster.  
    - `kubectl exec -it zookeeper-0 -- zkCli.sh`: optional in different tab. Can show zk state with `ls -R /` or `get /leader`  
    - `kubectl get services -w` or `kubectl get all`: optional to wait for IP to be assigned by loadbalancer  
    - `./azure_build yaml_fail`: removes old pods and launches new ones, with the failing pods starting first  
3. Explain [data structure replication decisions](#data-structure-replication-decisions) (< 2 minutes)  
4. Explain [timeout/heartbeat decisions](#timeoutheartbeat-decisions) (< 2 minutes)  
5. Run the word counter application with the small dataset, M=20, R=5  
    - `./client.sh -c 25mb -m 20 -r 5`  
    - Show how it handled *master* failure  
      - `kubectl logs master-fail<tab>`: before failure. May need `--previous` if it crashed hard.  
        - Data was replicated to follower while processing  
      - `kubectl logs master-good<tab>`: after failure  
        - Data was received from leader
    - Show how it handled *worker* failure  
      - `kubectl logs worker-fail<tab>`: the worker that failed  
        - The worker received a task and then went into its fail state
      - The failure can be seen on one of the masters. It will have been subsequently requeued.  
6. Run the word counter application with the big dataset, M=100, R=20  
    - `./client.sh -c 250mb -m 100 -r 20`  
    - Show some of the output files  

Might also show:  
- The log on master showing the heartbeat being performed and the status of each worker  
- The logs for the master leader replicating data to the master followers  

---
## Data Structure Replication Decisions  
The masters store the following data:  
  - The current request id  
  - Number of mappers  
  - Number of reducers  
  - The source container name  
  - The list of map tasks, including portions of input files  
  - The list of completed map tasks, including the intermediate files they created  
  - The list of reduce tasks that had succeeded  

When the leader finds that one or more asyncronous map or reduce jobs has completed, it replicates this data to the followers. The followers immediately update their local state to reflect the cluster's current job.  
We use RPC for the sync because it allows us to push data directly to the other masters. We don't need to worry about the extra overhead of creating, updating, or accessing blobs.  
We start the leader's RPC clients when it receives its first job. Other than when the leader is put into its failure state, the leader does not stop replication.  
We start a follower's RPC server when it discovers that it is not the leader. It will stop the server when it becomes the leader. At that time, the new leader will look for any remaining map tasks and perform them. Then it will do the same with any remaining reduce tasks.  
We assume only one job will run on our MVP MapReduce cluster at once, so each master only stores data for the current job. This way we don't need to worry about which master is in charge of which job or which task we are updating. If multiple jobs could be handled at once and needed to be syncronized, we would need to consider which completed tasks could safely be cleaned up out of memory.  

---
## Timeout/Heartbeat Decisions  
We update the list of workers either when the job arrives at the leader's http server or when a new leader starts working on a replicated job.  
We start heartbeats just before starting to assign map jobs to the clients and stop just after the reduce jobs are all complete. We mainly do this to avoid cluttering the log.  
We send an asyncronous heartbeat check every 5 seconds to every worker.  
We store the current heartbeat result for each worker. This status is determined solely by the current leader and is not replicated. This data is used to avoid assigning tasks to workers whose heartbeats fail or timed out after 10 seconds.  

---
## Work Distribution  
- Fail based on yaml/exe input: Jonathan  
- Worker heartbeat: Pavan  
- Worker failure handling: Pavan and Jonathan  
- Master data replication: Pavan  
- Master failure handling: Jonathan and Pavan  
- Refactoring: Pavan and Jonathan  
- Update client script: Jonathan  
- Documentation: Jonathan  
- Various independent debugging: Pavan and Jonathan  
- Various pair development debugging: Pavan and Jonathan  

---
## Prerequisites  
- Run `install.sh` in the w6 directory to install all libraries needed for this workshop sequence  
