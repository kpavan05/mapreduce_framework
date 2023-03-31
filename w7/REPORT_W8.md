# Workshop 8 Report  

Return to workshop 8 [README](../w6/docs/workshop8.md)  

## Client Implementation  
The client script uploads files into an azure blob storage container in the same storage account.  
The client script then posts the job to the HTTP server (which is the leader node) with the container name as a query parameter:  
    `curl -X POST http://<server_ip_address>:8000?container=<container_name>`  

The script supports a variety of arguments, like:  
- `./client.sh --container test1 --files src/python/map.py,src/python/reduce.py`  
- `./client.sh --contatiner test1 -folder src/files`  
- The `--container`, `--files`, and `--folder` arguments can be shorted to `-c`, `-f`, and `-F` respectively.  

If only a container is specified: it assumes that the container already exists and contains the desired files.  
If a file or a folder is specified: it creates the container and uploads the files or the contents of the folder.  
The script automatically prompts for login if the user is not logged in. It automatically finds the server IP address.  
If the intermediate and output containers already exist, it will delete their content before posting the job to our map/reduce HTTP service.  

---
## Master Implementation (including the Leader)  
Once a master becomes the leader and connects to the storage account, it starts its HTTP server and marks itself so the kubernetes readiness probe sees that it is ready.  

The leader node then gets the container name from the request query parameters and connects to it. It gets the list of blobs in the container and calculates their total size. It then performs sharding with each shard given a shard id and list of offsets for the file(s) in that shard. The total size of data in each shard is approximately equal for all shards in a job. The number of shards is equal to the number of workers.  

The leader then makes async an RPC call with shard information to each worker. It uses a completion queue to receive responses from the workers. Each response received is a list of intermediate blobs (pre-split for the reduce tasks) and the intermediate container.  

Once all map tasks are complete, the leader assigns reduce tasks to the workers. The RPC message for a reduce task lists all blobs ending with a suffix that identifies that reduce task. The number of reduce tasks is equal to the number of workers.  

---
## Worker Implementation  

We use the embedded python object model to invoke the python scripts and get the results.  

### Map  
- Worker creates empty intermediate files as there are number of reducers. If there are 3 reducers in the system, then we create file with name `int_<worker_address>_0`, `int_<worker_address>_1`, `int_<worker_address>_2`, etc.  
- Downloads shard data to temporary file on disk  
- Invokes python script with the temporary file. Result from python script is streamed as follows:  
   - Hash of word is done using (std::hash<string> % number of reducers)  
   - Using the hash value, the key value is put into the `int_<worker_address>_<reducer_number>`.  
     For example: If the hash of the word returns 1, then the key value pair is written in the file `int_<worker_address>_1`  
- All intermediate files are then uploaded to different container with name `<original container name>_int`.  
- Return the blob list and container name to the leader. The leader collects all intermediate blobs with the same hash value suffix and sends them to a worker as a reduce task.  

### Reduce  
- From the list of blobs received from rpc call, uploads the data of each blob into one temporary file on disk.  
- Invokes python script which streams the data to an output file.  
- Output file is then uploaded to storage accounter in a different container with `<original_container_name>_out`.  
- Returns the container name to master.  
