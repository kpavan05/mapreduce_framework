#include "master.h"
#include "replserver.h"

/*
void ReplicationServiceImpl::RunServer(std::string addr, bool & done)
{
    LOG(INFO) << "start replication server ";
    try {
        int pos = addr.find(":");
        std::string host_addr = addr.substr(0, pos);
        std::string server_address = host_addr + ":" + std::to_string(LEADER_GRPC_PORT);

        grpc::EnableDefaultHealthCheckService(true);
        grpc::reflection::InitProtoReflectionServerBuilderPlugin();
        ServerBuilder builder;
        // Listen on the given address without any authentication mechanism.
        builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
        // Register "service" as the instance through which we'll communicate with
        // clients. In this case it corresponds to an *synchronous* service.
        builder.RegisterService(&service_);

        //cq_ = builder.AddCompletionQueue();
        // Finally assemble the server.
        server_ = builder.BuildAndStart();
        //std::unique_ptr<Server> server(builder.BuildAndStart());
        LOG(INFO) << "Server listening on " << server_address;

        // Wait for the server to shutdown. Note that some other thread must be
        // responsible for shutting down the server for this call to ever return.
        handleRpcs(done);
        //server_.wait();
    }
    catch (std::exception e)
    {
        LOG(INFO) <<"exception thrown while starting replication server";
    }
}
*/

/*
Status ReplicationServiceImpl::CheckPointCallData::do_work(ServerContext* context, const NoParam* request, CheckPointData* response)
{
    Master * m = static_cast<Master *>(parentObj);
    m->serialize_data(response);
    return Status::OK;
}
*/


void ReplicationServiceImpl::RunServer(std::string addr, bool &done)
{
  int pos = addr.find(":");
  std::string host_addr = addr.substr(0, pos);
  std::string server_address = host_addr + ":" + std::to_string(LEADER_GRPC_PORT);

  grpc::EnableDefaultHealthCheckService(true);
  grpc::reflection::InitProtoReflectionServerBuilderPlugin();
  ServerBuilder builder;
  // Listen on the given address without any authentication mechanism.
  builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
  // Register "service" as the instance through which we'll communicate with
  // clients. In this case it corresponds to an *synchronous* service.
  builder.RegisterService(this);
  // Finally assemble the server.
  server_ = builder.BuildAndStart();
  LOG(INFO) << "Server listening on " << server_address << std::endl;

  // Wait for the server to shutdown. Note that some other thread must be
  // responsible for shutting down the server for this call to ever return.
  server_->Wait();
}

void ReplicationServiceImpl::sendData(CheckPointData* reply)
{
    Master * m = static_cast<Master *>(this->parent_);
    m->serialize_data(reply);
}

void ReplicationServiceImpl::getData(const CheckPointData* request)
{
    Master * m = static_cast<Master *>(this->parent_);
    m->deserialize_data(request);
}


