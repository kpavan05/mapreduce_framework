## Demo  
1. Explain [design](./design.md)  
    - Filesystem interfaces used (< 2 minutes)  
    - Sharding algorithm: show code and explain key components (< 5 minutes)  
2. Use the 2 largest files from the Gutenberg data set (container: `two-files`)  
3. The master node will shard the files and distribute the shards via RPC (1 master and 3 workers)  
4. Worker nodes will read the shards into memory, append gatech, then save each shard back to Azure  
5. Show that this worked using Azure Storage Explorer  

## Prerequisites  
- Run `install.sh` in the w6 directory to install all libraries needed for this workshop sequence  
