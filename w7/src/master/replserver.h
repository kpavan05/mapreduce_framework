#pragma once

#include <exception>
#include <grpcpp/ext/proto_server_reflection_plugin.h>
#include <grpcpp/grpcpp.h>

#include "mr.grpc.pb.h"
#include <glog/logging.h>


using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::ServerAsyncResponseWriter;
using grpc::ServerCompletionQueue;
using grpc::Status;
using grpc::StatusCode;

using mr::NoParam;
using mr::CheckPointData;
using mr::ReplicationService;

#define LEADER_GRPC_PORT 6060
#define FOLLOWER_GRPC_PORT 6061


// Logic and data behind the server's behavior.
class ReplicationServiceImpl final: public ReplicationService::Service
{
private:
    std::unique_ptr<Server> server_;
    //std::unique_ptr<ServerCompletionQueue> cq_;
    //ReplicationService::AsyncService service_;
    void * parent_;
    std::string address_;
public:

  ~ReplicationServiceImpl()
  {
    // server_->Shutdown();
    // Always shutdown the completion queue after the server.
    //cq_->Shutdown();
  }

  void cleanup() {
    server_->Shutdown();
  }


  void set_parent (void * parent) { parent_ = parent; }
  void set_address (std::string addr) {address_ = addr;}

  void RunServer(std::string addr, bool &done);
  Status sendCheckPointData(ServerContext* context, const NoParam* request, CheckPointData* reply) override
  {
    sendData(reply);

    return Status::OK;
  }


  Status pushCheckPointData(ServerContext* context, const CheckPointData* request, NoParam* reply) override
  {
    
    getData(request);
    return Status::OK;
  }

private:
  void sendData(CheckPointData * reply);
  void getData(const CheckPointData *) ;
    /*
    class CheckPointCallData
    {
    protected:
        enum CallStatus { CREATE, PROCESS, FINISH };
        CallStatus status_;
        ReplicationService::AsyncService * service_;
        ServerCompletionQueue* completionQueue_;
        NoParam request_;
        CheckPointData reply_;
        ServerAsyncResponseWriter<CheckPointData> responder_;
        ServerContext serverContext_;
        void * parentObj;
    public:
        CheckPointCallData(ReplicationService::AsyncService* service, ServerCompletionQueue* completionQueue, void * parent) :
            status_(CREATE),
            service_(service),
            completionQueue_(completionQueue),
            responder_(&serverContext_),
            parentObj(parent)
        {
        }
    public:
        void Proceed()
        {
            if (status_ == CREATE)
            {
                status_ = PROCESS;
                LOG(INFO) << "server got checkpoint request ";
                WaitForRequest();
            }
            else if (status_ == PROCESS)
            {
                AddNextToCompletionQueue();

                Status s = HandleRequest();
                status_ = FINISH;

                responder_.Finish(reply_, s, this);
            }
            else
            {
                // We're done! Self-destruct!
                if (status_ != FINISH)
                {
                    // Log some error message
                }
                delete this;
            }
        }
    protected:
        void AddNextToCompletionQueue()
        {
            new CheckPointCallData(service_, completionQueue_, parentObj);
        }
        void WaitForRequest()
        {
            service_->RequestsendCheckPointData(&serverContext_, &request_, &responder_, completionQueue_, completionQueue_, this);
        }
        Status HandleRequest()
        {
            LOG(INFO) << "process checkpoint request on server";
            return do_work(&serverContext_, &request_, &reply_);
        }

        Status do_work(ServerContext* context, const NoParam* request, CheckPointData* response);

    };

    private:
    void handleRpcs(bool & done)
    {
        new CheckPointCallData(&service_, cq_.get(), parent_);
        void* tag;
        bool ok;
        while (!done) {
            bool ret = cq_->Next(&tag, &ok);
            if (ok == false || ret == false)
            {
                return;
            }
            static_cast<CheckPointCallData*>(tag)->Proceed();
        }
    }
    */
};
