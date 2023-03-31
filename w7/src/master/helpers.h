#pragma once

#include <string>
#include <list>
#include <vector>
#include <map>
#include <queue>

#include <glog/logging.h>


enum class ClientStatus {
	IDLE,
	MAP_RUNNING,
	REDUCE_RUNNING,
	MAP_DONE,
	REDUCE_DONE,
	BROKEN,
  DEADLINE_EXCEED,
};

enum class PingStatus {
  PING_CHECK,
  PING_SUCCESS,
  PING_FAIL
};

enum class ReturnStatus {
	FAILED,
	SUCCESS,
	TIMEDOUT
};
class MapperResult
{
public:
  MapperResult (ReturnStatus s, int sid, std::string c, bool x, std::vector<std::string> v)
  {
        status = s; shardId = sid; cname = c; isrequeued = x;
        if (v.size() > 0)
          blobList.assign(v.begin(), v.end());
        else
          blobList.clear();
  }
	ReturnStatus status;
	int shardId;
	std::string cname;
  std::vector<std::string> blobList;
  bool isrequeued;
};

class ReduceResult
{
public:
  ReduceResult (ReturnStatus s, std::string b, std::string c, int j, bool x)
  {
        status = s;  cname = c; blobname = b; jobid = j; isrequeued = x;
  }
	ReturnStatus status;
	std::string cname;
  std::string blobname;
  int jobid;
  bool isrequeued;
};

class GenericResult
{
public:
	ReturnStatus status;
};


typedef struct shard_info shard_info;
struct shard_info {
  public:
     shard_info (std::string s, size_t begin, size_t end, bool x)
     {
        blob_name = s; begin_offset = begin; end_offset = end; is_partial = x;
     }
  std::string blob_name;
  size_t begin_offset;
  size_t end_offset;
  bool is_partial;
};

typedef struct client_info ClientInfo;
struct client_info {
  public:
  client_info(std::string addr, std::string id)
  {
    nodeid = id;
    address = addr;
  }
  std::string address;
  std::string nodeid;
};

typedef struct ShardData ShardData;
struct ShardData {
public:
     ShardData (std::string s, size_t begin, size_t end )
     {
        blobName_ = s; begin_ = begin; end_ = end;
     }

     std::string blobName_;
     size_t begin_;
     size_t end_;

};

typedef struct FileShard FileShard;
struct FileShard {
public:
     FileShard(int id, std::vector<ShardData> ss)
     {
        shardId_ = id;
        sets_.clear();
        sets_.insert(sets_.end(), ss.begin(), ss.end());
     }

     int shardId_;
     std::vector<ShardData> sets_;
};

static int getShardId(bool reset = false)
{
     static int id_ = -1;
     if (reset) {
       id_ = -1;
       return id_;
     }
     return ++id_;
}



/////// High level design for how we might have been done differently.
/////// Mainly: 1) group more data into classes and 2) have the master push decisions about names
/////// As long we don't need to remember the old requests, there's no need to refactor (means a different server MUST reply to client)
// Self serializing/deserializing state (would add constructor, serialize/deserialize for master sync and worker messaging, custom to_string)
// Master has a list of jobs, a list of workers, and only a few other variables
//   Supports job status/complete request
// Worker might store a list of tasks it has been assigned and completed. Will have slightly different information stored on its side.
//   If worker receives duplicate job and job is complete, can return that fact
// How do we ensure a new Leader can recover communication with worker without leaving data in a weird state? Need to either cut off the old task or sync new request to old.

// Master needs create_job function

/*
class MapReduceJob {
  public:
  int request_id;
  int n_mappers;
  int n_reducers;
  std::string map_func;
  std::string reduce_func;

  std::string src_container;
  std::string int_container;
  std::string out_container;

  time_t start_time;
  time_t end_time;

  JobPhase phase; // for convenience

  std::vector<MapTask*> map_tasks;
  std::vector<ReduceTask*> reduce_tasks;

  // Lifecycle:
  // Set phase to initial
  // Initialize names of containers and create all MapTasks
  // Set phase to map. Start assigning jobs until all complete
  // Create all ReduceTasks
  // Set phase to reduce. Start assigning reduce jobs until all complete
  // Mark as complete
}

enum class JobPhase {
	INITIAL,
  MAP,
  REDUCE,
  COMPLETE,
};

class GenericTask {
  public:
  int task_id;
  MapReduceJob* job; // For convenience
  Worker* assigned_to; // (master only) override assigned_to and assigned_time if re-assigned
  time_t assigned_time;
  time_t completed_time; // if present, task is done
  // int job_id // (worker only)
  // in/out containers, map or reduce func, number of reducers // (worker only)
}

class MapTask: public GenericTask {
  public:
  std::vector<ShardPieces> shard_pieces;
  std::string blob_out_base_name;
}

class ReduceTask: public GenericTaskk {
  public:
  std::vector<std::string> int_file_names; // could use shard pieces again and combine the map output blobs...
  std::string blob_out_name;
}

class ShardPiece {
  public:
  std::string blob_name;
  int begin;
  int end;
}

class Worker {
  public:
  std::string node_path; // This is the worker's canonical name
  std::string ip_address;
  PingStatus ping_status;
  // ClientStatus work_status;
  GenericTask* task_assigned;
  MapReduceClient* client;
}
*/
