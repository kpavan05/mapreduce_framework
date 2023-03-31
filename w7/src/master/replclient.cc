#include "master.h"
#include "replclient.h"

/*
// Assembles the client's payload, sends it and presents the response back from the server.
void ReplicationServiceClient::ReplicateJobStatus(void * obj)
{
    NoParam query;
    CompletionQueue cq;
    AsyncClientCheckPointCall *call = new AsyncClientCheckPointCall();


    LOG(INFO) << "making async checkpoint call id " << call;
    call->response_reader = stub_->PrepareAsyncsendCheckPointData(&call->context, query, &cq);

    // StartCall initiates the RPC call
    call->response_reader->StartCall();

    call->response_reader->Finish(&call->reply, &call->status, (void*)call);


    void* got_tag;
    bool ok = false;

    GPR_ASSERT(cq.Next(&got_tag, &ok));
    GPR_ASSERT(ok);

    AsyncClientCheckPointCall* ret_call = static_cast<AsyncClientCheckPointCall*>(got_tag);
    if (ret_call->status.ok())
    {
        Master * m = static_cast<Master *>(obj);
        m->deserialize_data(ret_call->reply);
    }
    else
    {
        LOG(INFO) << ret_call->status.error_code() << ": " << ret_call->status.error_message();
    }

    delete call;
}
*/
// keep it simple with synchronous call
void ReplicationServiceClient::ReplicateJobStatus(void * obj)
{
    NoParam query;
    CheckPointData reply;
    ClientContext context;

    LOG(INFO) << "making checkpoint call";
    // The actual RPC.
    Status status =  stub_->sendCheckPointData(&context, query, &reply);
    if (status.ok())
    {
      Master * m = static_cast<Master *>(obj);
      m->deserialize_data(&reply);
    }
    else
    {
      LOG(INFO) << status.error_code() << ": " << status.error_message()<< std::endl;
    }

}


void ReplicationServiceClient::PushJobStatus(void * obj)
{
    NoParam reply;
    CheckPointData query;
    ClientContext context;

    LOG(INFO) << "making checkpoint call";

    Master * m = static_cast<Master *>(obj);
    m->serialize_data(&query);
    // The actual RPC.
    Status status =  stub_->pushCheckPointData(&context, query, &reply);
    if (status.ok())
    {
      LOG(INFO) << "data is replicated ";
    }
    else
    {
      LOG(INFO) << status.error_code() << ": " << status.error_message()<< std::endl;
    }

}
