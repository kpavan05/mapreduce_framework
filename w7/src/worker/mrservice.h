#ifndef _MR_SERVICE_H
#define _MR_SEERVICE_H

#include <grpcpp/ext/proto_server_reflection_plugin.h>
#include <grpcpp/grpcpp.h>
#include <grpcpp/health_check_service_interface.h>

#include "mr.grpc.pb.h"
#include <glog/logging.h>
#include "worker.h"

using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::ServerAsyncResponseWriter;
using grpc::ServerCompletionQueue;
using grpc::Status;
using grpc::StatusCode;
using mr::MasterInfo;
using mr::WorkerInfo;
using mr::MapperInput;
using mr::ReducerInput;
using mr::IntermediateResult;
using mr::FinalResult;
using mr::IntermediateData;
using mr::ShardTuple;
using mr::NoParam;
using mr::WorkerStatus;
using mr::MapReduceService;


using namespace std;
#define WRKR_AVAILABLE 0
#define WRKR_BUSY 1
#define WRKR_DONE 2
enum class WorkStatus {
	IDLE,
	MAP_RUNNING,
	REDUCE_RUNNING,
	MAP_DONE,
	REDUCE_DONE
};


// Logic and data behind the server's behavior.
class MapReduceServiceImpl final : public MapReduceService::Service {
public:

  ~MapReduceServiceImpl() {
    server_->Shutdown();
    // Always shutdown the completion queue after the server.
    cq_->Shutdown();
  }

  void RunServer(std::string ipAddress, int port, Worker * obj)
  {

    std::string server_address = ipAddress + ":" + std::to_string(port);


    set_parent(obj);

    grpc::EnableDefaultHealthCheckService(true);
    grpc::reflection::InitProtoReflectionServerBuilderPlugin();
    ServerBuilder builder;
    // Listen on the given address without any authentication mechanism.
    builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
    // Register "service" as the instance through which we'll communicate with
    // clients. In this case it corresponds to an *synchronous* service.
    builder.RegisterService(&service_);

    cq_ = builder.AddCompletionQueue();
    // Finally assemble the server.
    server_ = builder.BuildAndStart();
    //std::unique_ptr<Server> server(builder.BuildAndStart());
    LOG(INFO) << "Server listening on " << server_address;

    // Wait for the server to shutdown. Note that some other thread must be
    // responsible for shutting down the server for this call to ever return.
    handleRpcs();
  }

  void set_parent(Worker *obj) { parent = obj ;}




  Status getWorkerStatus(ServerContext* context, const NoParam* request, WorkerStatus* response)
  {
    switch (status_)
    {
      case WorkStatus::IDLE:
        response->set_status(WRKR_AVAILABLE);
        break;
      case WorkStatus::MAP_RUNNING:
      case WorkStatus::REDUCE_RUNNING:
        response->set_status(WRKR_BUSY);
        break;
      case WorkStatus::MAP_DONE:
      case WorkStatus::REDUCE_DONE:
        response->set_status(WRKR_DONE);
        break;
    }
    return Status::OK;
  }
  /*
  Status ExchangeInfo(ServerContext* context, const MasterInfo* request, WorkerInfo* reply) override
  {

    if (parent)
    {
      LOG(INFO) << "########## ExchangeInfo: containername:" << request->containername() << " ##########";
      parent->create_connection(request->connectionstring(), request->containername());
      reply->set_message(parent->get_address());
      status_ = WorkStatus::IDLE;
    }
    else
    {
      reply->set_message("");
    }
    return Status::OK;
  }

  Status Ping(ServerContext* context, const NoParam* request, NoParam * reply) override
  {
    {
      LOG(INFO) << "########## ping reached :" << parent->get_address() << " ##########";

    }

    return Status::OK;
  }
  */
  private:
    void handleRpcs()
    {
        new ExchangeInfoCallData(&service_, cq_.get(), parent);
        new PingCallData(&service_, cq_.get(), parent);
        new MapCallData(&service_, cq_.get(), parent);
        new ReduceCallData(&service_, cq_.get(), parent);

        void* tag;
        bool ok;
        while (true) {
            bool ret = cq_->Next(&tag, &ok);
            if (ok == false || ret == false)
            {
                return;
            }
            static_cast<BaseCallData*>(tag)->Proceed();
        }
    }

  private:

    class BaseCallData
    {
      protected:
          virtual void WaitForRequest() = 0;
          virtual Status HandleRequest() = 0;
      public:
          virtual void Proceed() = 0;
          BaseCallData() {}
    };


    template < class RequestType, class ReplyType>
    class CommonCallData : BaseCallData
    {
    protected:
        enum CallStatus { CREATE, PROCESS, FINISH };
        CallStatus status_;
        MapReduceService::AsyncService * service_;

        ServerCompletionQueue* completionQueue_;
        RequestType request_;
        ReplyType reply_;
        ServerAsyncResponseWriter<ReplyType> responder_;
        ServerContext serverContext_;
        Worker * parent;

        // When we handle a request of this type, we need to tell
        // the completion queue to wait for new requests of the same type.
        virtual void AddNextToCompletionQueue() = 0;
    public:
        CommonCallData(MapReduceService::AsyncService* service, ServerCompletionQueue* completionQueue, Worker * parentObj) :
            status_(CREATE),
            service_(service),
            completionQueue_(completionQueue),
            responder_(&serverContext_),
            parent(parentObj)
        {
        }
    public:
        virtual void Proceed() override
        {
            if (status_ == CREATE)
            {
                status_ = PROCESS;
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
    };

    class MapCallData : CommonCallData<MapperInput, IntermediateResult>
    {
    public:
        MapCallData(MapReduceService::AsyncService* service, ServerCompletionQueue* completionQueue, Worker * parentObj)
          : CommonCallData(service, completionQueue, parentObj)
        {
            Proceed();
        }
    protected:
        virtual void AddNextToCompletionQueue() override
        {
            new MapCallData(service_, completionQueue_, parent);
        }
        virtual void WaitForRequest() override
        {
            service_->RequestmapTask(&serverContext_, &request_, &responder_, completionQueue_, completionQueue_, this);
        }
        virtual Status HandleRequest() override
        {
            LOG(INFO) << "map request reached";
            return do_work(&serverContext_, &request_, &reply_);
        }

        Status do_work(ServerContext* context, const MapperInput* request, IntermediateResult* response)
        {
          int nReducers = request->npartition();
          int shard_id = request->shard_id();
          std::string req_id = request->req_id();

          LOG(INFO) << "########## MapperInput received: npartition:" << std::to_string(nReducers)
            << " shard_id:" << std::to_string(shard_id)
            << " req_id:" << req_id << " ##########";
          parent->task_assigned_check_fail();

          std::vector<std::string> blobList;
          std::string rescontainer;
          for(int piece_id = 0 ; piece_id < request->stuples_size(); piece_id++)
          {
            ShardTuple stuple = request->stuples(piece_id);
            LOG(INFO) << "  Shard part:" << std::to_string(piece_id)
              << " blobname:" << stuple.blobname()
              << " startoffset:" << std::to_string(stuple.startoffset())
              << " endoffset:" << std::to_string(stuple.endoffset());

            parent->map_shard(stuple.blobname(), stuple.startoffset(), stuple.endoffset(), nReducers, shard_id, piece_id, rescontainer, blobList);
          }

          response->set_containername(rescontainer) ;

          for (auto blob : blobList)
          {
              IntermediateData * data = response->add_blobs();
              data->set_blobname(blob);
              LOG(INFO) << "intermediate blob sent back to master " << blob;
          }

          // if (serverContext_.IsCancelled()) {
          //      return Status(StatusCode::CANCELLED, "Deadline exceeded or Client cancelled, abandoning.");
          // }
          LOG(INFO) << "map request processed";
          return Status::OK;
        }

    };

    class ReduceCallData : CommonCallData<ReducerInput, FinalResult>
    {
    public:
        ReduceCallData(MapReduceService::AsyncService* service, ServerCompletionQueue* completionQueue, Worker * parentObj)
          : CommonCallData(service, completionQueue, parentObj)
        {
            Proceed();
        }
    protected:
        virtual void AddNextToCompletionQueue() override
        {
            new ReduceCallData(service_, completionQueue_, parent);
        }
        virtual void WaitForRequest() override
        {
           service_->RequestreduceTask(&serverContext_, &request_, &responder_, completionQueue_, completionQueue_, this);
        }
        virtual Status HandleRequest() override
        {
          LOG(INFO) << "reduce request reached";
          return do_work(&serverContext_, &request_, &reply_);
        }

        Status do_work(ServerContext* context, const ReducerInput* request, FinalResult* response)
        {
          std::string container = request->containername();
          std::string req_id = request->req_id();
          int reducer_id = request->reducer_id();


          std::string resblob;
          std::string rescontainer;
          std::string tmp_filename = "kv-" + parent->get_address() + '-' + std::to_string(reducer_id);
          std::ofstream tmp_file;
          tmp_file.open(tmp_filename, std::ofstream::trunc | std::ofstream::binary);

          LOG(INFO) << "########## ReducerInput: containername:" << container << " req_id:" << req_id << " ##########";
          parent->task_assigned_check_fail();
          LOG(INFO) << "blobs size:" << request->blobs_size();
          for(int i = 0 ; i < request->blobs_size(); i++)
          {
            IntermediateData d = request->blobs(i);
            parent->prepare_data(container, d.blobname(), tmp_file);

          }
          tmp_file.close();

          parent->reduce(container, tmp_filename, reducer_id, resblob, rescontainer);

          response->set_containername(rescontainer);
          LOG(INFO) << "reduce task is executed and final container name is " << rescontainer;
          // if (serverContext_.IsCancelled()) {
          //      return Status(StatusCode::CANCELLED, "Deadline exceeded or Client cancelled, abandoning.");
          // }
          LOG(INFO) << "reduce request processed";
          return Status::OK;
        }
    };

    class ExchangeInfoCallData : CommonCallData<MasterInfo, WorkerInfo>
    {
    public:
        ExchangeInfoCallData(MapReduceService::AsyncService* service, ServerCompletionQueue* completionQueue, Worker * parentObj)
          : CommonCallData(service, completionQueue, parentObj)
        {
            Proceed();
        }
    protected:
        virtual void AddNextToCompletionQueue() override
        {
            new ExchangeInfoCallData(service_, completionQueue_, parent);
        }
        virtual void WaitForRequest() override
        {
            service_->RequestExchangeInfo(&serverContext_, &request_, &responder_, completionQueue_, completionQueue_, this);
        }
        virtual Status HandleRequest() override
        {
            LOG(INFO) << "exchange request reached";
            return do_work(&serverContext_, &request_, &reply_);
        }

        Status do_work(ServerContext* context, const MasterInfo* request, WorkerInfo* reply)
        {
          if (parent)
          {
            LOG(INFO) << "########## ExchangeInfo: containername:" << request->containername() << " ##########";
            parent->create_connection(request->connectionstring(), request->containername());
            reply->set_message(parent->get_address());
          } else
          {
            reply->set_message("");
          }
          LOG(INFO) << "exchange request processed";
          return Status::OK;
        }
    };

    class PingCallData : CommonCallData<NoParam, NoParam>
    {
    public:
        PingCallData(MapReduceService::AsyncService* service, ServerCompletionQueue* completionQueue, Worker * parentObj)
          : CommonCallData(service, completionQueue, parentObj)
        {
            Proceed();
        }
    protected:
        virtual void AddNextToCompletionQueue() override
        {
            new PingCallData(service_, completionQueue_, parent);
        }
        virtual void WaitForRequest() override
        {
            service_->RequestPing(&serverContext_, &request_, &responder_, completionQueue_, completionQueue_, this);
        }
        virtual Status HandleRequest() override
        {
          LOG(INFO) << "ping request processed";
          return Status::OK;
        }


    };

  private:
    Worker * parent;
    WorkStatus status_;

    std::unique_ptr<Server> server_;
    std::unique_ptr<ServerCompletionQueue> cq_;
    MapReduceService::AsyncService service_;
};


#endif
