#pragma once

#include <iostream>
#include <memory>
#include <string>

// #include <thread>
// #include <sys/stat.h>
// #include <sys/types.h>
// #include <dirent.h>
//#include <list>

#include <grpcpp/grpcpp.h>

#include "mr.grpc.pb.h"
#include "helpers.h"

using grpc::Channel;
using grpc::ClientContext;
using grpc::Status;
using grpc::CompletionQueue;
using grpc::ClientAsyncResponseReader;

using mr::MasterInfo;
using mr::WorkerInfo;
using mr::NoParam;
using mr::ShardTuple;
using mr::MapperInput;
using mr::ReducerInput;
using mr::IntermediateData;
using mr::IntermediateResult;
using mr::FinalResult;
using mr::WorkerStatus;
using mr::MapReduceService;

class MapReduceClient;

// struct for keeping map state and data information
struct AsyncClientMapCall
{
      // Container for the data we expect from the server.
      IntermediateResult reply;

      // Context for the client. It could be used to convey extra information to
      // the server and/or tweak certain RPC behaviors.
      ClientContext context;

      // Storage for the status of the RPC upon completion.
      Status status;

      int shardId;
      std::string address;
      MapReduceClient  *client;
      std::unique_ptr<ClientAsyncResponseReader<IntermediateResult>> response_reader;
};

struct AsyncClientReduceCall
{
      // Container for the data we expect from the server.
      FinalResult reply;

      // Context for the client. It could be used to convey extra information to
      // the server and/or tweak certain RPC behaviors.
      ClientContext context;

      // Storage for the status of the RPC upon completion.
      Status status;
      int jobid;
      std::string address;
      MapReduceClient  *client;
      std::unique_ptr<ClientAsyncResponseReader<FinalResult>> response_reader;
};

struct AsyncClientExchangeCall
{
      // Container for the data we expect from the server.
      WorkerInfo reply;

      // Context for the client. It could be used to convey extra information to
      // the server and/or tweak certain RPC behaviors.
      ClientContext context;

      // Storage for the status of the RPC upon completion.
      Status status;

      MapReduceClient  *client;

      std::unique_ptr<ClientAsyncResponseReader<WorkerInfo>> response_reader;
};

struct AsyncClientPingCall
{
      // Container for the data we expect from the server.
      NoParam reply;

      // Context for the client. It could be used to convey extra information to
      // the server and/or tweak certain RPC behaviors.
      ClientContext context;

      // Storage for the status of the RPC upon completion.
      Status status;
      std::string address;
      MapReduceClient  *client;
      std::unique_ptr<ClientAsyncResponseReader<NoParam>> response_reader;
};


class MapReduceClient {
 public:
  MapReduceClient(std::shared_ptr<Channel> channel)
      : stub_(MapReduceService::NewStub(channel))
  {
    clientStatus_ = ClientStatus::IDLE;
    nMapRequests = 0;
    nReduceRequests = 0;
  }

  ClientStatus getStatus() {return clientStatus_;}
  std::string get_address() {return address_;}
  PingStatus getPingStatus() { return pingStatus_ ;}
  int getNumMapRequests() { return nMapRequests;} 
  int getNumReduceRequests() { return nReduceRequests;}
  void set_address(std::string addr) { address_ = addr ;}

  void setStatus(ClientStatus status)
  {

    {
      std::unique_lock<std::mutex> lock(mtx_);
      clientStatus_ = status;
    }

  }

  void setPingStatus(PingStatus status)
  {
     pingStatus_ = status;
  }

  void check_liveness(CompletionQueue *cq)
  {
      NoParam query;

      // Call object to store rpc data
      AsyncClientPingCall *call = new AsyncClientPingCall;

      std::chrono::system_clock::time_point deadline = std::chrono::system_clock::now() + std::chrono::milliseconds(10000);
      call->context.set_deadline(deadline);
      call->address = address_;
      call->client = this;
      LOG(INFO) << "making async ping call to address " << address_ << " and call id " << call;
      call->response_reader = stub_->PrepareAsyncPing(&call->context, query, cq);

      // StartCall initiates the RPC call
      call->response_reader->StartCall();

      call->response_reader->Finish(&call->reply, &call->status, (void*)call);

  }

   void ExchangeInfo(const std::string& myAddress, std::string & connString, std::string containername, CompletionQueue *cq)
  {
      // Data we are sending to the server.
      MasterInfo query;
      query.set_name(myAddress);
      query.set_connectionstring(connString);
      query.set_containername(containername);

      // Call object to store rpc data
      AsyncClientExchangeCall* call = new AsyncClientExchangeCall;
      std::chrono::system_clock::time_point deadline = std::chrono::system_clock::now() + std::chrono::milliseconds(10000);
      call->context.set_deadline(deadline);
      call->client = this;

      LOG(INFO) << "making async exchange info call id is:" << call;
      call->response_reader = stub_->PrepareAsyncExchangeInfo(&call->context, query, cq);

      call->response_reader->StartCall();

      call->response_reader->Finish(&call->reply, &call->status, (void*)call);
  }

  void invokeMapTaskAsync(FileShard &fileShard, int nPartitions, int req_id, CompletionQueue * cq)
  {
    MapperInput query;
    for(auto item : fileShard.sets_)
    {
      ShardTuple *sset = query.add_stuples();
      sset->set_blobname(item.blobName_);
      sset->set_startoffset(item.begin_);
      sset->set_endoffset(item.end_);
    }
    query.set_req_id(std::to_string(req_id));
    query.set_npartition(nPartitions);
    query.set_shard_id(fileShard.shardId_);

    // Call object to store rpc data
    AsyncClientMapCall *call = new AsyncClientMapCall();

    std::chrono::system_clock::time_point deadline = std::chrono::system_clock::now() + std::chrono::milliseconds(10000);
    call->context.set_deadline(deadline);
    call->address = address_;
    call->shardId = fileShard.shardId_;
    call->client = this;
    nMapRequests++;
    LOG(INFO) << "making async map call to address " << address_ <<  " for shard " << fileShard.shardId_ << " call id" << call;
    call->response_reader = stub_->PrepareAsyncmapTask(&call->context, query, cq);

    // StartCall initiates the RPC call
    call->response_reader->StartCall();

    
    call->response_reader->Finish(&call->reply, &call->status, (void*)call);

    
  }

  void invokeReduceTaskAsync(std::vector<std::string> blobs, std::string containername, int req_id, int reducer_id, CompletionQueue * cq)
  {
    ReducerInput query;
    for(auto item : blobs)
    {
      IntermediateData *d = query.add_blobs();
      d->set_blobname(item);
    }
    query.set_containername(containername);
    query.set_req_id(std::to_string(req_id));
    query.set_reducer_id(reducer_id);

    AsyncClientReduceCall *call = new AsyncClientReduceCall();

    std::chrono::system_clock::time_point deadline = std::chrono::system_clock::now() + std::chrono::milliseconds(10000);
    call->context.set_deadline(deadline);
    call->address = address_;
    call->client = this;
    call->jobid = reducer_id;

    LOG(INFO) << "making async  reduce call to address " << address_ << " call id" << call;
    nReduceRequests++;
    call->response_reader = stub_->PrepareAsyncreduceTask(&call->context, query, cq);

    // StartCall initiates the RPC call
    call->response_reader->StartCall();
    
    call->response_reader->Finish(&call->reply, &call->status, (void*)call);
    
  }


 private:
  std::unique_ptr<MapReduceService::Stub> stub_;
  std::string address_;
  std::mutex mtx_;
  ClientStatus clientStatus_;
  PingStatus pingStatus_;

  int nMapRequests;
  int nReduceRequests;
};
