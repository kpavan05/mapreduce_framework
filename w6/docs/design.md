# Design  

[Return to README](../README.md)  
[Return to workshop 7](./workshop7.md)  

---
## RPC Spec for Communication between Master and Worker  
The master sends a *request*, and the worker returns a *response*  

### Ask worker to shard file  
TODO Do we need this? It seems like it would be useful. I think it's OK to split one file at a time.  
Request: filename, number of parts  
Response: filename, list of 

### Send map info to worker  
TODO  

### Send reduce info to worker  
TODO  

### Ping for status (workshop 9)  
From master to worker  
Any response indicates that the worker is still alive and working.  
TODO

---
## Sharding algorithm  

### Requirements:  
- Be able to accept multiple input files. (We will start with a container identifier.)  
- ~Have a configurable minimum and maximum shard size. (This is not an actual requirement.)~  
- Try to spread the input equally across all the mappers.  

Have workers split the files. Leader chooses how many splits per file. Each map task does not need to be of equal size, but may send larger tasks first.  
Need to handle aggregating multiple files into a task.  
How do workers handle receiving multiple files? I think they just always emit one file.  

### Algorithm  
1. Leader starts with the container, number of mappers, and number of reducers  
    - Later, this will come from the HTTP server  
2. Leader gets metadata for the files and calculates the total file size  
3. Leader gets number of workers available  
4. Split up sections of files as evenly as possible (TODO more detail)  
5. Store these decisions in zookeeper?  
6. Send the file ids and ranges to the workers  

TODO map -> reduce shard handling? Assign one file at a time to reducers, and if there are too many reducers then some go unused?  
Sharding decisions can be made by workers and sent back to the master  

---
## Key / Value pairs  
- Input: file_name / file_contents *(These come from the input container)*  
- Intermediate: word / count *(This is decided by the mapper)*  
- Output: word / count *(This is decided by the reducer)*  

---
## Filesystem Spec  
TODO interfaces MapReduce needs from the filesystem  
"The framework should strive to reduce the number of queries and the complexity of retrieving the data for each of the phases."  
Use file ranges for reads. What about writes?  
Should intermediate data be stored to local or blob storage? (Leaning towards local storage)  
Do we output a single Reduce file? Or one file per reducer? (Leaning towards one output file per input file)  
Mention atomic operations?  

---
## Web Server Spec (workshop 8)  

### POST job  
Creates the job  
A simple C++ exe will act as the client  
Accepts 1) input file location(s), 2) map function (.py file), 3) reduce function (.py file), 4) # of mappers, 5) # of reducers  
Returns 1) output file location(s)  

The output stored after a Map Reduce must be in the same format as we expect from input. This enables Map Reduce functions to be chained.  

---
## Zookeeper data structure  
["Zookeeper is designed to store data on the order of kilobytes in size."](https://zookeeper.apache.org/doc/r3.1.2/zookeeperAdmin.html) It should not be of O(M) or O(R) where M and R are the number of map and reduce tasks respectively. It can be O(W) where W is the number of workers.    

- `/masters`  
  - `master_#`: ephemeral nodes for each master  
- `/leader`: an ephemeral node created for the leader. It's disappearance means a new leader must be elected.  
- `/workers`
  - `worker_#`: ephemeral node for each worker. The data is the worker's IP address.  

---
## Other data  
- `tasks`
  - `#`: TODO status (idle/started/complete), start time, expected finish time?, assigned to, input, outputs, etc.
- File data? (input, intermediate, output)  

---
## API exposed to Python  

### map  
- Inputs: String `key`, String `value`  
- Output: tuple of two Strings  

### reduce  
- Inputs: String `key`  
- Output: String  
