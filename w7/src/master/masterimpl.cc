/*
 *
 * Parts of this project use samples from the gRPC authors. This license information is for those parts only.
 * Copyright 2015 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include "master.h"
#include "requesthandler.h" // This uses Boost, which has a macro named "U". Must be included before cppnetlib is defines a macro with the same name.
#include "dbconn.h"
#include "mrclient.h"
#include "replserver.h"
#include "replclient.h"
#include <grpcpp/grpcpp.h>

#include <iostream>
#include <string>
#include <chrono>
#include <thread>
#include <sys/types.h>
#include <fstream>
#include "mr.grpc.pb.h"
#include "mrclient.h"
#include <conservator/ConservatorFrameworkFactory.h>
#include <zookeeper/zookeeper.h>


using mr::CheckPointData;
using mr::JobStatus;
using mr::FileShardSet;
using mr::ReduceJobStatus;

bool g_ready = true;
std::mutex g_mtx_;
std::condition_variable g_cond_;

bool g_repl_data = true;
std::mutex g_repl_mtx_;
std::condition_variable g_repl_cond_;
std::vector<MapperResult> g_repl_list;

std::mutex g_response_mtx_;

#define PING_INTERVAL 5000

void AsyncCompleteMapRpc(CompletionQueue & cq, std::vector<MapperResult> & resList, int &nrpcs, bool & done)
{
    void* got_tag;
    bool ok = false;

    // Block until the next result is available in the completion queue "cq".
    while (!done)
    {
        if (nrpcs > resList.size())
        {
          bool ret = cq.Next(&got_tag, &ok);
          if (ok == false || ret == false)
          {
              return;
          }
          // The tag in this example is the memory location of the call object
          AsyncClientMapCall* call = static_cast<AsyncClientMapCall*>(got_tag);
          LOG(INFO) << "got result from address " << call->address << " for shard id " << call->shardId << " and " << call;
          if (call->status.ok())
          {
              std::vector<std::string> v;
              for(int i = 0; i < call->reply.blobs_size(); i++)
              {
                  IntermediateData curdata = call->reply.blobs(i);
                  v.push_back(curdata.blobname());
                //   LOG(INFO) << "intermediate blob received by master " << curdata.blobname();
              }
              {
                  std::unique_lock<std::mutex> lk(g_response_mtx_);
                  resList.emplace_back(ReturnStatus::SUCCESS, call->shardId, call->reply.containername(), false, v);
              }
              call->client->setStatus(ClientStatus::MAP_DONE);
              {
                std::unique_lock<std::mutex> lk(g_repl_mtx_);
                g_repl_data = true;
                g_repl_list.emplace_back(ReturnStatus::SUCCESS, call->shardId, call->reply.containername(), false, v);
                g_repl_cond_.notify_all();
              }
          }
          else
          {
              ReturnStatus rs;
              std::vector<std::string> v;
              v.clear();
              if (call->status.error_code() == 4)
              {
                rs = ReturnStatus::TIMEDOUT;
                call->client->setStatus(ClientStatus::DEADLINE_EXCEED);
              }
              else
              {
                LOG(INFO) << call->status.error_code() << ": " << call->status.error_message() << std::endl;
                call->client->setStatus(ClientStatus::BROKEN);
                rs = ReturnStatus::FAILED;
              }
              {
                  std::unique_lock<std::mutex> lk(g_response_mtx_);
                  resList.emplace_back(rs, call->shardId, "", false, v);
              }
              LOG(INFO) << "Map RPC failed" << std::endl;

          }
          call->client = nullptr;
          // Once we're complete, deallocate the call object.
          delete call;
        }
    }
}

void AsyncCompleteReduceRpc(CompletionQueue & cq, std::vector<ReduceResult> & resList, int &nrpcs, bool & done)
{
    void* got_tag;
    bool ok = false;

    // Block until the next result is available in the completion queue "cq".
    while (!done)
    {
        if (nrpcs > resList.size())
        {
          bool ret = cq.Next(&got_tag, &ok);
          if (ok == false || ret == false)
          {
              return;
          }
          // The tag in this example is the memory location of the call object
          AsyncClientReduceCall* call = static_cast<AsyncClientReduceCall*>(got_tag);
          LOG(INFO) <<"got result for reducer task from address " << call->address << " and "<< call;
          if (call->status.ok())
          {
              call->client->setStatus(ClientStatus::REDUCE_DONE);
              {
                  std::unique_lock<std::mutex> lk(g_response_mtx_);
                  resList.emplace_back(ReturnStatus::SUCCESS, call->reply.containername(), call->reply.blobname(), call->jobid, false);
              }
          }
          else
          {
              //ReduceResult *res = new ReduceResult();
              ReturnStatus rs;
              if (call->status.error_code() == 4)
              {
                rs = ReturnStatus::TIMEDOUT;
                call->client->setStatus(ClientStatus::DEADLINE_EXCEED);
              }
              else
              {
                LOG(INFO) << call->status.error_code() << ": " << call->status.error_message() << std::endl;
                rs = ReturnStatus::FAILED;
                call->client->setStatus(ClientStatus::BROKEN);
              }
              {
                  std::unique_lock<std::mutex> lk(g_response_mtx_);
                  resList.emplace_back(rs, "", "", call->jobid, false);
              }
              LOG(INFO) << "Reduce RPC failed" << std::endl;

          }
          call->client = nullptr;
          // Once we're complete, deallocate the call object.
          delete call;
        }
    }
}

void AsyncCompleteExchangeRpc(CompletionQueue & cq, int &nrpcs)
{
  void* got_tag;
  bool ok = false;
  int nrcvd = 0;

  // Block until the next result is available in the completion queue "cq".
  while (nrcvd < nrpcs)
  {

    bool ret = cq.Next(&got_tag, &ok);
    if (ok == false || ret == false)
    {
        return;
    }
    // The tag in this example is the memory location of the call object
    AsyncClientExchangeCall* call = static_cast<AsyncClientExchangeCall*>(got_tag);
    LOG(INFO) << "got result for exchange call id " << call;
    if (call->status.ok())
    {
        call->client->set_address(call->reply.message());
        call->client->setPingStatus(PingStatus::PING_SUCCESS);
    }
    else
    {
        call->client->setPingStatus(PingStatus::PING_FAIL);
        if (call->status.error_code() == 4)
        {
          call->client->setStatus(ClientStatus::DEADLINE_EXCEED);
        }
        else
        {
          LOG(INFO) << call->status.error_code() << ": " << call->status.error_message() << std::endl;
          call->client->setStatus(ClientStatus::BROKEN);
        }
        LOG(INFO) << "Exchange RPC failed" << std::endl;
    }

    call->client = nullptr;
    // Once we're complete, deallocate the call object.
    delete call;
    nrcvd++;
  }
}

void AsyncCompletePingRpc(CompletionQueue & cq, int &nrpcs)
{
    void* got_tag;
    bool ok = false;
    int nrcvd = 0;
    // Block until the next result is available in the completion queue "cq".
    while (nrcvd < nrpcs)
    {
        bool ret = cq.Next(&got_tag, &ok);
        if (ok == false || ret == false)
        {
            return;
        }
        // The tag in this example is the memory location of the call object
        AsyncClientPingCall* call = static_cast<AsyncClientPingCall*>(got_tag);
        GenericResult res;
        LOG(INFO) << "got result for ping call id " << call;
        if (call->status.ok())
        {
            res.status = ReturnStatus::SUCCESS;
            call->client->setPingStatus(PingStatus::PING_SUCCESS);
        }
        else
        {
            call->client->setPingStatus(PingStatus::PING_FAIL);
            if (call->status.error_code() == 4)
            {
              res.status = ReturnStatus::TIMEDOUT;
              call->client->setStatus(ClientStatus::DEADLINE_EXCEED);
            }
            else
            {
              LOG(INFO) << call->status.error_code() << ": " << call->status.error_message() << std::endl;
              res.status = ReturnStatus::FAILED;
              call->client->setStatus(ClientStatus::BROKEN);
            }

            LOG(INFO) << "Ping RPC failed" << std::endl;

        }
        call->client = nullptr;
        // Once we're complete, deallocate the call object.
        delete call;

        nrcvd++;
    }
}



bool watchTriggered =false;

void get_watcher_fn(zhandle_t *zh, int type, int state, const char *path,void *watcherCtx) {
    LOG(INFO) << "get watcher function called for path: " << path;
    if (state == 0  || state == ZOO_EXPIRED_SESSION_STATE)
    {
      LOG(INFO) << "leader is down";
    }
    ConservatorFramework* framework = (ConservatorFramework *) watcherCtx;
    // reset the watch
    if(framework->checkExists()->forPath(path)) {
        framework->getData()->withWatcher(get_watcher_fn, watcherCtx)->forPath(path);
    }
}

string state2String(ZOOAPI int state){
  if (state == 0)
    return "CLOSED_STATE";
  if (state == ZOO_CONNECTING_STATE)
    return "CONNECTING_STATE";
  if (state == ZOO_ASSOCIATING_STATE)
    return "ASSOCIATING_STATE";
  if (state == ZOO_CONNECTED_STATE)
    return "CONNECTED_STATE";
 if (state == ZOO_EXPIRED_SESSION_STATE)
    return "EXPIRED_SESSION_STATE";
  if (state == ZOO_AUTH_FAILED_STATE)
    return "AUTH_FAILED_STATE";

  return "INVALID_STATE";
}

void exists_watcher_fn(zhandle_t *zh, int type, int state, const char *path,void *watcherCtx) {
    LOG(INFO) << "exists watcher function called for path " << path << " and state is " << state2String(state);

    ConservatorFramework* framework = (ConservatorFramework *) watcherCtx;

    auto ret = framework->checkExists()->forPath("/leader");
    if (ret == ZOK) {
      LOG(INFO) << "leader exists";
    }
    else
    {
      watchTriggered = true;
      LOG(INFO) << "leader node does not exist";
    }
    LOG(INFO) << "resetting the watch " << path;
    framework->checkExists()->withWatcher(exists_watcher_fn, watcherCtx)->forPath("/leader");
}



Master::Master(int fail_after)
{
    m_address = "";

    m_framework = nullptr;
    m_nodepath.clear();

    m_has_failed = false;
    if (fail_after <= 0)
    {
        m_will_fail = false;
        m_until_fail = 0;
        LOG(INFO) << "This master is not assigned to fail";
    } else
    {
        m_will_fail = true;
        m_until_fail = fail_after;
        LOG(INFO) << "This master will fail after assigning " << std::to_string(m_until_fail) << " tasks";
    }

    m_connection = "";
    m_dbConnection = nullptr;

    m_Clients.clear();
    m_ClientMap.clear();

    m_stop_repl_as_leader = true;
    m_stop_repl_as_follower = true;
    m_map_done = true;

    m_hbcheck = false;
    m_bReplicated = false;

    m_server = nullptr;

    m_rid = 0;
    m_nmappers = 0;
    m_nreducers = 0;
    m_containername = "";
    m_shardsz = 0;
    m_shards.clear();
    m_ShardClientMapper.clear();

    m_IntermediateResultMap.clear();
    m_IntblobList.clear();
    m_intcontainer = "";

    m_OutputResultMap.clear();
}

Master::~Master() {
    m_server->stop();
    m_framework->close();
}

void Master::run() {
    m_address = getSrvAddress() + ":5050";

    init_zk_framework();
    create_connection(AZURE_CONNECTION_STRING);
    register_with_zk();
    update_workers(); // Initial time. This will happen again whenever a request/job is received.
    wait_until_leader();
    run_as_leader();
}

void Master::start_repl_server(ReplicationServiceImpl &r, bool &done)
{
    r.set_parent(this);
    r.RunServer(m_address, done);
    //RunReplicationServer(m_address, this, done);
}

bool Master::doesReplMasterExists()
{
    if (m_has_failed) {
        LOG(INFO) << "Ignoring doesReplMasterExists because this master has failed";
        return false;
    }
    std::vector<string> children = m_framework->getChildren()->forPath("/masters"); 
    return children.size() > 1;
}
void Master::start_repl_client(/*std::string ipAddress,*/bool &repldone)
{
    if (m_has_failed) {
        LOG(INFO) << "Ignoring start_repl_client because this master has failed";
        return;
    }
    std::vector<string> children = m_framework->getChildren()->forPath("/masters");
    std::string path = "/masters/" + children.at(1); // second child node
    std::string ip_wport = m_framework->getData()->forPath(path);

    int pos = ip_wport.find(":");
    std::string repl_srv_id = ip_wport.substr(0, pos);
    

    repl_srv_id += ":";
    repl_srv_id += std::to_string(LEADER_GRPC_PORT);
    LOG(INFO) << "ip of other master is " << repl_srv_id;

    //ReplicationServiceClient replClient (grpc::CreateChannel(ipAddress, grpc::InsecureChannelCredentials()));
    ReplicationServiceClient replClient (grpc::CreateChannel(repl_srv_id, grpc::InsecureChannelCredentials()));
    while (!repldone && g_repl_data)
    {
        //replClient.ReplicateJobStatus(this);
        replClient.PushJobStatus(this);
        {
            std::unique_lock<std::mutex> lk(g_repl_mtx_);
            g_repl_data = false;
            g_repl_cond_.wait(lk, []{return g_repl_data;});
        }
    }
        //std::this_thread::sleep_for(std::chrono::milliseconds(10000));
    
}

void Master::serialize_data(CheckPointData * reply)
{
    LOG(INFO) << "Replication send: serializing data ...";
    LOG(INFO) << "Serializing m_IntermediateResultMap size: " << std::to_string(g_repl_list.size());
    // for(auto itr = m_IntermediateResultMap.begin(); itr != m_IntermediateResultMap.end(); itr++)
    // {
    //     JobStatus * j = reply->add_job_list();
    //     j->set_shard_id(itr->first);
    //     j->set_int_container(itr->second.cname);
    //     for(auto item : itr->second.blobList)
    //     {
    //         IntermediateData *d = j->add_blob_list();
    //         d->set_blobname(item);
    //     }
    // }
    for (auto item : g_repl_list)
    {
        JobStatus * j = reply->add_job_list();
        j->set_shard_id(item.shardId);
        j->set_int_container(item.cname);
        for(auto blob : item.blobList)
        {
            IntermediateData *d = j->add_blob_list();
            d->set_blobname(blob);
        }
    }

    for(auto s: m_shards)
    {
        FileShardSet *f = reply->add_shard_list();
        f->set_shard_id(s.shardId_);

        for(auto x : s.sets_)
        {
            ShardTuple *sset = f->add_stuples();
            sset->set_blobname(x.blobName_);
            sset->set_startoffset(x.begin_);
            sset->set_endoffset(x.end_);
        }
    }

    for(auto itr = m_OutputResultMap.begin(); itr != m_OutputResultMap.end(); itr++)
    {
        ReduceJobStatus * r = reply->add_reduce_job_list();
        r->set_jobid(itr->first);
        r->set_out_container(itr->second);
    }
    reply->set_in_container(m_containername);
    reply->set_req_id(m_rid);
    reply->set_n_mappers(m_nmappers);
    reply->set_n_reducers(m_nreducers);
    LOG(INFO) << "Replication send: serializing done...";
}

void Master::deserialize_data(const CheckPointData * reply)
{
    m_rid = reply->req_id();
    m_nmappers = reply->n_mappers();
    m_nreducers = reply->n_reducers();
    m_containername = reply->in_container();

    LOG(INFO) << "Replication recv: deserializing data rid:"<< m_rid << ", nmappers:" << m_nmappers << ", nreducers:" << m_nreducers
              << ", container:" << m_containername;
    m_shards.clear();
    for(int i = 0; i < reply->shard_list_size(); i++)
    {
        FileShardSet fsdata = reply->shard_list(i);
        std::vector<ShardData> sets;
        for (int j = 0; j < fsdata.stuples_size(); j++)
        {
            ShardTuple x = fsdata.stuples(j);
            sets.emplace_back(x.blobname(), x.startoffset(), x.endoffset());
        }
        m_shards.emplace_back( fsdata.shard_id(), sets);
    }
    LOG(INFO) << "Replication recv: deserialized shard list";

    m_IntermediateResultMap.clear();
    m_IntblobList.clear();
    for(int i = 0; i < reply->job_list_size(); i++)
    {
        JobStatus js = reply->job_list(i);
        m_intcontainer = js.int_container();
        std::vector<std::string> v;

        for(int j = 0 ; j < js.blob_list_size(); j++)
        {
            v.push_back(js.blob_list(j).blobname());
        }
        MapperResult res( ReturnStatus::SUCCESS,js.shard_id(), m_intcontainer,false, v);
        m_IntermediateResultMap.emplace(std::make_pair(js.shard_id(), res));
        m_IntblobList.insert(m_IntblobList.end(), v.begin(), v.end());
    }
    LOG(INFO) << "Added intermediate: " << std::to_string(m_IntermediateResultMap.size()) << " of " << std::to_string(reply->job_list_size());

    m_OutputResultMap.clear();
    for(int i = 0; i < reply->reduce_job_list_size(); i++)
    {
        ReduceJobStatus r = reply->reduce_job_list(i);
        m_OutputResultMap.emplace(std::make_pair(r.jobid(), r.out_container()));
    }
    m_bReplicated = true;
    LOG(INFO) << "Replication recv: deserializing done ...";
}

void Master::processReplicatedData()
{
    update_workers();
    get_worker_addresses();

    std::queue<int> job_q;
    LOG(INFO) << "processing replicated data";
    LOG(INFO) << "size of m_IntermediateResultMap: " << std::to_string(m_IntermediateResultMap.size());
    std::thread t (&Master::start_ping, this);
    for (auto shard : m_shards)
    {
        int shardId = shard.shardId_;
        if (m_IntermediateResultMap.find(shardId) != m_IntermediateResultMap.end())
        {
            continue;
        }
        LOG(INFO) << "Adding " << std::to_string(shardId);
        job_q.push(shardId);
    }
    LOG(INFO) << "Dispatching map tasks from replicated data";
    dispatchMapTasks(job_q);
    LOG(INFO) << "Dispatching reduce tasks from replicated data";
    runReducer();

    stop_ping();
    t.join();

    LOG(INFO) << "processing replication data done...";   
}

void Master::set_num_mappers(std::string smappers)
{
    m_nmappers = strtol(smappers.c_str(), nullptr, 10);
    LOG(INFO) << "number of mappers " << m_nmappers;
}

void Master::set_num_reducers(std::string sreducers)
{
    m_nreducers = strtol(sreducers.c_str(), nullptr, 10);
    LOG(INFO) << "number of reducers " << m_nreducers;
}

void Master::update_workers() {
    // We can assume that it is ok to only look for new workers at startup and when starting a new job/request.
    // Assume that workers never change their IP address.

    if (m_has_failed) {
        LOG(INFO) << "Ignoring update_workers because this master has failed";
        return;
    }
    LOG(INFO) << "Starting to update worker list";

    // Fetch from zookeeper
    std::vector<string> wrkrs;
    try {
        m_framework->create()->forPath("/workers"); // Make sure this exists
        wrkrs = m_framework->getChildren()->forPath("/workers");
    } catch (std::exception e) {
        // Somehow checkExists could fail but not getChildren. Handling it this way instead.
        // This catch doesn't work... Hopefully the create call is fixing it.
        LOG(INFO) << "  Could not fetch workers from zookeeper. Assuming none exist.";
        wrkrs.clear();
    }

    LOG(INFO) << "  Number of workers: " << wrkrs.size();

    // Add locally
    for (int i = 0; i < wrkrs.size(); i++)
    {
        std::string wrkPath = "/workers/" + wrkrs.at(i);
        if (worker_client_already_added(wrkPath))
        {
            // LOG(INFO) << "  Worker already added " << wrkPath;
            continue;
        }
        std::string target_str = m_framework->getData()->forPath(wrkPath);
        MapReduceClient * client = new MapReduceClient(grpc::CreateChannel(target_str, grpc::InsecureChannelCredentials()));

        add_client(client, target_str, wrkPath);
        LOG(INFO) << "  Worker client added " << wrkPath;
    }
    LOG(INFO) << "  Done adding any new workers";

    // Remove locally
    remove:;
    for (int i = 0; i < m_Clients.size(); i++)
    {
        MapReduceClient* client = m_Clients.at(i);
        ClientInfo info = m_ClientMap.at(client);
        std::string wrkPath = info.nodeid;
        int pos = wrkPath.rfind('/');
        std::string stored_node_suffix = wrkPath.substr(pos+1);
        bool found = false;
        for (string received_node_suffix : wrkrs) {
            if (stored_node_suffix.compare(received_node_suffix) == 0) {
                found = true;
                break;
            }
        }
        if (!found) {
            LOG(INFO) << "  Removing worker client " << wrkPath;
            m_ClientMap.erase(client);
            m_Clients.erase(m_Clients.begin()+i);
            i--; // Keep i in same location on next loop iteration.
        }
    }
    LOG(INFO) << "  Done removing any missing workers";
}


bool Master::worker_client_already_added(std::string wrkPath) {
    for (MapReduceClient* client : m_Clients) {
        ClientInfo info = m_ClientMap.at(client);
        if (info.nodeid == wrkPath) {
            return true;
        }
    }
    return false;
}

void Master::add_client(MapReduceClient * client, std::string addr, std::string wrkrPath)
{
    ClientInfo info(addr, wrkrPath);
    m_ClientMap.insert(std::make_pair(client, info));
    m_Clients.push_back(client);
}

void Master::set_requestid(int id)
{
    update_workers(); // Started handling a new request. This side effect maybe shouldn't be here, but it works.
    m_rid = id;
}

void Master::create_connection(const utility::string_t & conn_string)
{
    m_connection = conn_string;
    m_dbConnection = new DBConnection(conn_string);
}

void Master::create_container(std::string container)
{
    m_containername = container;
    m_dbConnection->create_container(container);
}

void Master::start_ping()
{
    if (m_has_failed) {
        LOG(INFO) << "Ignoring start_ping because this master has failed";
        return;
    }
    m_hbcheck = true;
    LOG(INFO) << "start pinging ...";
    try
    {
        while (m_hbcheck)
        {
            LOG(INFO) << "ping thread is executed";
            ping_workers();
            std::this_thread::sleep_for(std::chrono::milliseconds(PING_INTERVAL));
        }
    } catch (std::exception e)
    {
        LOG(INFO) << "exception " << e.what();
    }
}

void Master::stop_ping()
{
    m_hbcheck = false;
    LOG(INFO) << "stop pinging ...";
}

void Master::ping_workers()
{
    CompletionQueue cq;
    int nrpcs = 0;

    for(auto curclient : m_Clients)
    {

    // LOG(INFO) << "pinging client ...";
    curclient->setPingStatus(PingStatus::PING_CHECK);
    curclient->check_liveness(&cq);
    nrpcs++;
    }
    AsyncCompletePingRpc(cq, nrpcs);
    {
        std::unique_lock<std::mutex> lk(g_mtx_);
        g_ready = true;
        g_cond_.notify_all();
    }
    LOG(INFO) << "ping call is done for all clients";
}

void Master::create_sharding_data()
{
    m_shards.clear();
    if (m_dbConnection == nullptr) return;
    m_shardsz = m_dbConnection->calc_shard_sz(m_nmappers);

    std::list<shard_info> shardList;
    m_dbConnection->make_shards(m_shardsz, shardList);

    //join partial shards into one FileShard
    std::vector<ShardData> info_set;
    info_set.clear();

    // reset the shard counter
    getShardId(true);
    for (auto item: shardList)
    {
        if(item.is_partial)
        {
            info_set.emplace_back(item.blob_name, item.begin_offset, item.end_offset);
        }
        else
        {
            info_set.emplace_back(item.blob_name, item.begin_offset, item.end_offset);
            m_shards.emplace_back(getShardId(), info_set);
            info_set.clear();
        }
    }
    if (info_set.size() > 0)
    {
        m_shards.emplace_back(getShardId(), info_set);
    }
    print_shards();

    // start leader as replication client. first point where we have some useful data
    if (doesReplMasterExists())
    {
        repl_clt_thd = std::thread(&Master::start_repl_client, this, std::ref(m_stop_repl_as_leader));
    }
    
}

void Master::print_shards()
{
    for (int i = 0; i < m_shards.size(); i++)
    {
        for (auto x : m_shards[i].sets_)
        {
            LOG(INFO) << "shard info name:" << x.blobName_ << " offsets:" << x.begin_ <<" and "<< x.end_ <<" shard id: " << m_shards[i].shardId_;
        }
    }
}

void Master::get_worker_addresses()
{
    CompletionQueue  cq;
    int nrpcs = 0;

    auto itr = m_ClientMap.begin();
    for(itr; itr != m_ClientMap.end(); itr++)
    {
        MapReduceClient * curclient = itr->first;
        curclient->ExchangeInfo(m_address, m_connection, m_containername, &cq);
        nrpcs++;
    }
    AsyncCompleteExchangeRpc(cq, nrpcs);

    LOG(INFO) << "exchange call is done for all clients";
}

void Master::runMapper()
{
    m_IntblobList.clear();
    m_IntermediateResultMap.clear();

    std::queue<int> job_q;
    for (int i = 0; i < m_shards.size(); i++) 
    { 
      job_q.push(i);
    }
    dispatchMapTasks(job_q);
}

void Master::dispatchMapTasks(std::queue<int> & job_q)
{
    LOG(INFO) << " start map tasks. task queue length: " << job_q.size() << "  shards: " << m_shards.size();

    CompletionQueue cq;
    std::vector<MapperResult> responseList;
    responseList.clear();

    int nrpcs = 0;
    m_map_done = false;
    bool check = false;

    m_t1 = std::thread(AsyncCompleteMapRpc, std::ref(cq), std::ref(responseList), std::ref(nrpcs), std::ref(m_map_done));
    //std::thread t2(CheckClientAvailability, std::ref(m_Clients), std::ref(m_map_done), std::ref(check));

    while (true)
    {
        int i = 0;
        if (job_q.size() > 0) {
            LOG(INFO) << "start dispatching the tasks in job queue... ";
            // LOG(INFO) << "nrpcs:" << nrpcs << " responseList.size():" << responseList.size() << " mapsize:" << m_IntermediateResultMap.size() << " last shard id:" << i;
        }
        while (job_q.size() > 0)
        {
            if (nrpcs > 10) { // m_Clients.size()
                LOG(INFO) << "nrpcs:" << nrpcs << " responseList.size():" << responseList.size() << " mapsize:" << m_IntermediateResultMap.size() << " last shard id:" << i;
                break;
            }
            i = job_q.front();
            // LOG(INFO) << "Client about to be found";
            MapReduceClient * curClient = findMinReqHealthyClient(false);
            // LOG(INFO) << "Client found";

            curClient->setStatus(ClientStatus::MAP_RUNNING);
            LOG(INFO) << "invoking task for shard id " << i << " on " << curClient->get_address();
            curClient->invokeMapTaskAsync(m_shards[i], m_nreducers, m_rid, &cq);
            m_ShardClientMapper[m_shards[i].shardId_] = curClient;
            nrpcs++;

            job_q.pop();
            task_assigned_check_fail();
        }

        // TODO should not wait for one round to complete before starting the next
        //   Should queue up one task per worker, then send a new task each time it completes one.
        //   If possible, I would make the loop handle both (new response to verify) and (shard task needing assignment && worker available) until all shards are complete
        LOG(INFO) << "all the tasks in job queue are dispatched";

        while(responseList.size() == 0);
        LOG(INFO) << "at least some of the responses in job queue are received";
        auto itr = responseList.begin();
        while (itr != responseList.end())
        {
            MapperResult res = *itr;
            if (res.status != ReturnStatus::SUCCESS)
            {
                LOG(INFO) << "[[[ failed map task is being re-enqueued: " << std::to_string(res.shardId) << " ]]]";
                job_q.push(res.shardId);
                {
                    std::unique_lock<std::mutex> lk(g_response_mtx_);
                    itr = responseList.erase(itr);
                }
                nrpcs--;
            }
            else
            {
                if (m_IntermediateResultMap.find(res.shardId) == m_IntermediateResultMap.end())
                {
                    m_IntermediateResultMap.emplace(std::make_pair(res.shardId, res));
                    // client->setStatus(ClientStatus::IDLE); // not used
                    for (auto item : res.blobList)
                    {
                        m_IntblobList.push_back(item);
                    }
                    m_intcontainer = res.cname;
                }
                {
                    std::unique_lock<std::mutex> lk(g_response_mtx_);
                    itr = responseList.erase(itr);
                }
                nrpcs--;
            }
        }

        if (m_IntermediateResultMap.size() == m_nmappers)
        {
            break;
        }
        else
        {
            // LOG(INFO) << "failed tasks are enqueued onto job queue";
        }
    }
    m_map_done = true;

    LOG(INFO) << "done with all async calls and response list size is " << responseList.size();
    try
    {
        responseList.clear(); // should already be clear
    }
    catch (std::exception e)
    {
        LOG(INFO) << "exception occurred " << e.what();
    }
    LOG(INFO) << "Mapping is fully complete";
}


void Master::runReducer()
{
    LOG(INFO) << " start reducer task";
    std::queue<int> job_q;
    for (int i = 0; i < m_nreducers; i++) 
    { 
      job_q.push(i);
    }

    dispatchReduceTasks(job_q);
    m_t1.join();
    m_r1.join();
}


MapReduceClient* Master::findHealthyClient()
{
    while(true)
    {
        for(auto client: m_Clients)
        {
            if (client->getPingStatus() == PingStatus::PING_SUCCESS)
            {
                return client;
            }
        }
        {
            std::unique_lock<std::mutex> lk(g_mtx_);
            g_ready = false;
            g_cond_.wait(lk, []{return g_ready;});
        }
    }
}

MapReduceClient* Master::findMinReqHealthyClient(bool bReduce)
{
    while(true)
    {
        MapReduceClient * min_req_client = nullptr;
        int nCurRequest = 0;
        int nMinRequest = 0;
        for(auto client: m_Clients)
        {
            if (client->getPingStatus() == PingStatus::PING_SUCCESS)
            {
                // find the client with less requests in flight for loada balance.
                if (min_req_client == nullptr)
                {
                    min_req_client = client;
                    continue;
                }
                nCurRequest = !bReduce ? client->getNumMapRequests() : client->getNumReduceRequests();
                nMinRequest = !bReduce ? min_req_client->getNumMapRequests() : min_req_client->getNumReduceRequests();

                if (nCurRequest < nMinRequest)
                {
                    min_req_client = client;
                }
            }
        }
        if (min_req_client != nullptr)
        {
            return min_req_client;
        }
        {
            std::unique_lock<std::mutex> lk(g_mtx_);
            g_ready = false;
            g_cond_.wait(lk, []{return g_ready;});
        }
    }
}

/*
void Master::dispatchReduceTasks()
{
    CompletionQueue cq;
    std::vector<ReduceResult> responseList;
    responseList.clear();
    int nrpcs = 0;
    bool done = false;

    std::thread r1(AsyncCompleteReduceRpc, std::ref(cq), std::ref(responseList), std::ref(nrpcs), std::ref(done));
    auto itr = m_ClientMap.begin();

    // TODO this sends a job even if there is no reducer task
    // TODO I don't think this does anything to recover from a reducer failure
    for (itr; itr != m_ClientMap.end(); itr++)
    {
        std::string path = itr->second.nodeid;
        LOG(INFO) << "client node path for reducer task " << path;
        int pos = path.find('_');
        std::string node_suffix = path.substr(pos+1);
        long id = std::stol(node_suffix);
        LOG(INFO) << "client node id for reducer task " << id;
        int n = id % m_ClientMap.size(); // TODO. This doesn't seem right...

        std::vector<std::string> reduce_blob_vec;

        for (auto item : m_IntblobList)
        {
            std::string x = item;
            int pos1 = x.find_last_of('_');

            std::string item_suffix = x.substr(pos1 + 1);
            int r = std::stol(item_suffix);

            if (r != n) continue;

            reduce_blob_vec.push_back(item);
        }
        std::string cname = m_intcontainer; 

        int reducer_id = n; // TODO
        itr->first->setStatus(ClientStatus::REDUCE_RUNNING);
        itr->first->invokeReduceTaskAsync(reduce_blob_vec, cname, m_rid, reducer_id, &cq);
        nrpcs++;
        task_assigned_check_fail();
    }

    while(responseList.size() != nrpcs); // TODO don't wait for a set to finish before sending jobs to idle workers

    done = true;
    LOG(INFO) << "done with all async calls and response list size is " << responseList.size();

    auto res_itr = responseList.begin();
    while (res_itr != responseList.end())
    {
        auto res = *(res_itr);
        if (res.status != ReturnStatus::SUCCESS)
        {
            res_itr++;
            continue;
        }
        res_itr++;
        LOG(INFO) << "processing response done ...";
    }

    r1.join();
}
*/

void Master::dispatchReduceTasks(std::queue<int> & job_q)
{
    CompletionQueue cq;
    m_OutputResultMap.clear();
    std::vector<ReduceResult> responseList;
    responseList.clear();
    int nrpcs = 0;
    bool done = false;

    m_r1 = std::thread(AsyncCompleteReduceRpc, std::ref(cq), std::ref(responseList), std::ref(nrpcs), std::ref(done));
 
    while (true)
    {
        LOG(INFO) << "Top of reduce task assignment loop IntblobList size: " << m_IntblobList.size();
        while (job_q.size() > 0) 
        {
            if (nrpcs - responseList.size() > m_Clients.size()) {
                break;
            }
            int jobid = job_q.front();
            std::vector<std::string> reduce_blob_vec;
            for (auto item : m_IntblobList)
            {
                std::string x = item;
                int pos1 = x.find_last_of('_');

                std::string item_suffix = x.substr(pos1 + 1);
                int r = std::stol(item_suffix);

                if (r != jobid) continue;

                reduce_blob_vec.push_back(item);
            }
            std::string cname = m_intcontainer; 

            MapReduceClient * client = findMinReqHealthyClient(true);

            client->setStatus(ClientStatus::REDUCE_RUNNING);
            LOG(INFO) << "invoking reduce task " << jobid << " on " << client->get_address();
            client->invokeReduceTaskAsync(reduce_blob_vec, cname, m_rid, jobid, &cq);
            nrpcs++;
            task_assigned_check_fail();
            job_q.pop();
        }

        LOG(INFO) << "Waiting for assigned reduce tasks to complete";
        while(responseList.size() != nrpcs); 
        LOG(INFO) << "Responses received";

        auto itr = responseList.begin();
        while (itr != responseList.end())
        {
            ReduceResult res = *itr;
            if (res.status != ReturnStatus::SUCCESS)
            {
                LOG(INFO) << "[[[ failed reduce task is being re-enqueued: " << std::to_string(res.jobid) << " ]]]";
                job_q.push(res.jobid);
                {
                    std::unique_lock<std::mutex> lk(g_response_mtx_);
                    itr = responseList.erase(itr);
                }
                nrpcs--;
            }
            else
            {
                itr++;
            }
        }
        if (job_q.size() == 0)
        {
            break;
        }
        else
        {
            // LOG(INFO) << "failed tasks are enqueued onto job queue";
        }
    }
    done = true;
    LOG(INFO) << "done with all async calls and response list size is " << responseList.size();

    auto res_itr = responseList.begin();
    while (res_itr != responseList.end())
    {
        auto res = *(res_itr);
        if (res.status != ReturnStatus::SUCCESS)
        {
            res_itr++;
            continue;
        }
        res_itr++;
        m_OutputResultMap.emplace(std::make_pair(res.jobid, res.cname));
        LOG(INFO) << "processing response done ...";
    }

    LOG(INFO) << "Reducing is fully complete";
}

void Master::task_assigned_check_fail() {
    if (m_will_fail) {
        m_until_fail--;
        if (m_until_fail <= 0) {
            enter_fail_state();
        }
        LOG(INFO) << std::to_string(m_until_fail) << " tasks until failure";
    }
}

void Master::enter_fail_state() {
    LOG(INFO) << "##### Entering fail state";
    m_has_failed = true;
    stop_ping(); // Stop checking on workers
    // for (MapReduceClient* client : m_Clients) {
        // client-> ???
        // I don't see a way to shutdown the connection from the client side. At best, a completion queue could be shutdown.
        // Shouldn't be an issue, since we aren't replicating and aren't sending out new messages.
    // }
    m_stop_repl_as_leader = true; // Stop replication. This isn't working, but can probably rely on other masters switching to the new leader.
    m_stop_repl_as_follower = true;
    m_map_done = true; // Stop async map
    m_server->stop(); // Stop web server
    m_framework->close(); // Stop ZK
    std::remove("leader_config.txt"); // delete config. Marks as not ready and removed from loadbalancer.
    LOG(INFO) << "Going to sleep";
    while (true) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    }
}

void Master::init_zk_framework() {
    // Create factory for framework
    ConservatorFrameworkFactory factory = ConservatorFrameworkFactory();

    // Get zk address
    std::string zkURL = "";
    #ifdef LOCAL
        zkURL = "localhost:2181";
    #else
        std::string ns = std::getenv("MY_POD_NAMESPACE");
        zkURL = "zookeeper." + ns + ".svc.cluster.local:2181";
    #endif

    // Create a new connection object to Zookeeper
    m_framework = factory.newClient(zkURL, 10000000);
    m_framework->start();
    LOG(INFO) << "Started connection to zookeeper";
}

std::string Master::getSrvAddress() {
    #ifdef LOCAL
        return "localhost";
    #else
        return std::getenv("MY_POD_IP");
    #endif
}

void Master::register_with_zk() {
    m_framework->create()->forPath("/masters");
    m_framework->create()->withFlags(ZOO_SEQUENCE|ZOO_EPHEMERAL)->forPath("/masters/master_", m_address.c_str(), m_nodepath);
    LOG(INFO) << "This master node has id " << m_nodepath;
}

void Master::wait_until_leader() {
    // Wait until leader
    bool isLeader = false;
    bool existLeader = false;

    auto ret = m_framework->checkExists()->withWatcher(exists_watcher_fn, ((void *) m_framework.get()))->forPath("/leader");
    if (ret != ZNONODE)
    {
        existLeader = true;
        LOG(INFO) << "leader exists";
    }
    tryAgain:;
    if ( !existLeader )
    {
        isLeader = elect_leader();
    }


    m_stop_repl_as_follower = false; // formerly called repl_done

    // We only need to support two masters. This note is just for 3+ masters.
    // update the master being tracked when a new leader is elected.
    // Could pass in a string to elect_leader to get the current leader?
    // Could switch to a string-bool map for the stop following signals?
    // Split out function verify_following_leader(leader_node_path)
    //   send stop signal to any thread not for the current leader
    //   if current leader is not already running, start it
    //   if empty string is passed, just stop all.

    if (!isLeader)
    {
        {
            ReplicationServiceImpl r;

            std::string leader_id = m_framework->getData()->forPath("/leader");
            if (leader_id.empty() || (leader_id.length() > 30)) {
                // This should only happen when there was an additional error.
                // leader_ip can have a crazy length like 1031138 when this happens.
                // LOG(INFO) << "leader id length is " << leader_id.length();
                LOG(INFO) << "IP was empty, likely due to a server restarting. Trying again in 2 seconds.";
                std::this_thread::sleep_for(std::chrono::milliseconds(2000));
                existLeader = m_framework->checkExists()->forPath("/leader");
                LOG(INFO) << "About to try again. /leader does " << (existLeader ? "" : "NOT ") << "exist";
                goto tryAgain;
            }
            LOG(INFO) << "leader node id is " << leader_id;
            std::string leader_address = m_framework->getData()->forPath("/masters/" + leader_id);

            int pos = leader_address.find(":");
            std::string leader_ip = leader_address.substr(0, pos);
            LOG(INFO) << "leader ip is " << leader_ip;

            leader_ip += ":";
            leader_ip += std::to_string(LEADER_GRPC_PORT);

            LOG(INFO) << "non leader master is creating server for replication. Current leader ip: " << leader_ip;

            //std::thread rc(&Master::start_repl_client, &*this, leader_ip, std::ref(m_stop_repl_as_follower));
            //rc.detach();
            repl_srv_thd = std::thread(&Master::start_repl_server, this, std::ref(r), std::ref(m_stop_repl_as_follower));
            // repl_srv_thd.detach();


            while (!isLeader)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(1000));
                if (watchTriggered)
                {
                    isLeader = elect_leader();
                }
            }

            m_stop_repl_as_follower = true;
            // LOG(INFO) << "before cleanup";
            r.cleanup();
            // LOG(INFO) << "after cleanup";
        }
        // LOG(INFO) << "after cleanup scope";

        if (repl_srv_thd.joinable())
        {
            repl_srv_thd.join();
        }
    }
    // The process says: processing replicated data, start map tasks, start dispatching the tasks in job queue...
    // But then it does nothing doe 10 seconds. Then does one last making checkpoint call and deserializing
    // Elect leader doesn't reach the point where it claims to be ready
}

bool Master::elect_leader()
{
    if (m_has_failed) {
        LOG(INFO) << "Ignoring elect_leader because this master has failed";
        return false;
    }
    watchTriggered = false;
    std::vector<string> children = m_framework->getChildren()->forPath("/masters");
    LOG(INFO) << "number of masters: " << children.size();

    if (children.size() > 1)
    {
      std::sort(children.begin(), children.end());

    }
    std::string path = "/masters/" + children.at(0);
    std::string id = m_framework->getData()->forPath(path);
    LOG(INFO) << "smallest znode is: " << path << " and id is " << id;

    if (path.compare(m_nodepath) == 0)
    {
        // you are the leader. create an ephemeral node /leader which will get deleted if you stop
        m_framework->create()->withFlags(ZOO_EPHEMERAL)->forPath("/leader");
        m_framework->setData()->forPath("/leader", children.at(0).c_str());
        //m_framework->setData()->forPath("/masters", children.at(0).c_str());
        LOG(INFO) << " I am the leader " << children.at(0) ;
        return true;
    }
    return false;
}

void Master::run_as_leader() {
    m_stop_repl_as_leader = false; // formerly named stop_repl
    // The replication client will start when an http job does to sharding

    LOG(INFO) << "Setting up http server";
    connection_handler handler(&*this);
    server::options options(handler);
    m_server = new server(options.address(getSrvAddress()).port("8000").reuse_address(false));
    //  .thread_pool(std::make_shared<boost::network::utils::thread_pool>(2)));

    LOG(INFO) << "Marking as ready for k8s";
    std::ofstream fp("leader_config.txt");
    fp << "leader: " << m_nodepath << std::endl;
    fp.close();

    // Handle any existing work from a previous master
    if (hasReplicationData())
    {
        processReplicatedData();
    }

    // Enter the server eventloop on main thread
    try {
        // LOG(INFO) << "Server entrance";
        m_server->run();
    } catch (std::exception e) {
        LOG(INFO) << "exception " << e.what();
    }
    LOG(INFO) << "Server has exited";

    // Clean up
    m_stop_repl_as_leader = true;
    if (repl_clt_thd.joinable())
    {
        repl_clt_thd.join();
    }
}
