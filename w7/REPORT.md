# Specification  
We use azure blob storage as a file system for this workshop and use azure storage library written in c++ for operations on the file system. BLob storage can handle three different blob types page blob, block blob and appen blob. For this workshop we are using block blob as the unit of IO.

## Master  
Following are the azure blob storage APIs that are called by master for IO operations to file system.  

parse(connection string)    -- creates cloud_storage_account  
create_cloud_blob_client () -- creates cloud_blob_client from the cloud_storage_account  
get_container_reference()   -- get the handle for blob container. if not exists, then create using create_if_not_exists api  
get_block_blob_reference()  -- using the container handle, we get handle for a block blob. block blob is a type of blob that fits our usecase  
                               this api creates a block blob with the name provided if does not exist.  
upload_from_stream()        -- this api is used to write to the block blob.  

## Worker:  
Along with the above APIs mentioned for master, worker uses  
download_range_to_stream()  -- this api is used for download content or read from the blob from a particular offset.  


# Implementation:  
Once the leader election is done, master creates the storage container named mr19container and uploads the files provided by users as blobs into the storage container. Each file is written as one blob. Master then gets the list of all blobs using list_blobs() api. This API returns an iterator which is used to loop through each blob and get the properties of each blob using properties() API. From the properties list, we get each blob size and sum up to running total to get the total blob size.  
Using the zookeeper, we get the number of workers and their IP addresses from workers/ directory tree. We split the total blob size equally among the workers to do further processing such as map and reduce. The process of splitting is called sharding and each split is called a shard.  
Each worker node has to process data of shard size.  

Shard info comprises of blob name, begin offset and end offset. A list of shards is maintanined by master. While sharding if the remaining size in the current blob is less than the calculated shard size, then remaining portion is obtained from next blob. We add two shards in the list but both these shards is processed by the same worker for now. This can be relaxed.  

Once the shard list is complete, we connect to the worker nodes using IP address. The grpc has two rpc methods for now, one is ExchangeInfo and MapShard. First it calls ExchangeInfo to provide connection string to worker node. Worker then connects to same storage account but it creates another container called  mr19shardcontainer where all the shard blobs are created.  
Using MapShard, master provides shard information to worker node. Worker then goes to mr19container and calls download_range_to_stream to get the data from the offset provided by master. Since we prefer to have word boundary while doing map reduce operations, we download extra bytes and round of the data. We always round ahead to be consistent between shards.  

Each Worker after rounding of the data for correct word boundary appends gatech and writes the shard to mr19shardcontainter i.e. the underlying file system.  
