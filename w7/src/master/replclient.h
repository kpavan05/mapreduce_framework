#pragma once

#include <iostream>
#include <memory>
#include <string>

#include <grpcpp/grpcpp.h>

#include "mr.grpc.pb.h"
#include <glog/logging.h>

using grpc::Channel;
using grpc::ClientContext;
using grpc::Status;
using grpc::CompletionQueue;
using grpc::ClientAsyncResponseReader;

using mr::NoParam;
using mr::CheckPointData;
using mr::ReplicationService;

class ReplicationServiceClient;

struct AsyncClientCheckPointCall
{
      // Container for the data we expect from the server.
      CheckPointData reply;

      // Context for the client. It could be used to convey extra information to
      // the server and/or tweak certain RPC behaviors.
      ClientContext context;

      // Storage for the status of the RPC upon completion.
      Status status;

      std::unique_ptr<ClientAsyncResponseReader<CheckPointData>> response_reader;
};

class ReplicationServiceClient {
private:
  std::unique_ptr<ReplicationService::Stub> stub_;

 public:
  ReplicationServiceClient(std::shared_ptr<Channel> channel)
      : stub_(ReplicationService::NewStub(channel))
  {
    LOG(INFO) <<"instantiating replication service ";
  }
  void ReplicateJobStatus(void * m);

  void PushJobStatus(void *m);
};
