/*
 *
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

#include <iostream>
#include <memory>
#include <string>

#include <chrono>
#include <thread>

#include <conservator/ConservatorFrameworkFactory.h>
#include <zookeeper/zookeeper.h>
#include <grpcpp/grpcpp.h>

#include "sample.grpc.pb.h"
#include <glog/logging.h>

using grpc::Channel;
using grpc::ClientContext;
using grpc::Status;
using helloworld::Greeter;
using helloworld::HelloReply;
using helloworld::HelloRequest;

bool watchTriggered =false;

class GreeterClient {
 public:
  GreeterClient(std::shared_ptr<Channel> channel)
      : stub_(Greeter::NewStub(channel)) {}

  // Assembles the client's payload, sends it and presents the response back
  // from the server.
  std::string SayHello(const std::string& user) {
    // Data we are sending to the server.
    HelloRequest request;
    request.set_name(user);

    // Container for the data we expect from the server.
    HelloReply reply;

    // Context for the client. It could be used to convey extra information to
    // the server and/or tweak certain RPC behaviors.
    ClientContext context;

    // The actual RPC.
    Status status = stub_->SayHello(&context, request, &reply);

    // Act upon its status.
    if (status.ok()) {
      return reply.message();
    } else {
      std::cout << status.error_code() << ": " << status.error_message()
                << std::endl;
      return "RPC failed";
    }
  }

 private:
  std::unique_ptr<Greeter::Stub> stub_;
};

void get_watcher_fn(zhandle_t *zh, int type, int state, const char *path,void *watcherCtx) {
    cout << "get watcher function called for path: " << path <<endl;
    if (state == 0  || state == ZOO_EXPIRED_SESSION_STATE)
    {
      std::cout << "leader is down"<<std::endl;
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
    std::cout << "exists watcher function called for path "<< path << " and state is " << state2String(state) << std::endl;

    ConservatorFramework* framework = (ConservatorFramework *) watcherCtx;

    auto ret = framework->checkExists()->forPath("/leader");
    if(ret == ZOK) {
      std::cout << "leader exists " << std::endl;
    }
    else
    {
      watchTriggered = true;
      std::cout << "leader node does not exist " << std::endl;
    }
    std::cout << " resetting the watch "<< path << std::endl;
    framework->checkExists()->withWatcher(exists_watcher_fn, watcherCtx)->forPath("/leader");
  
}

bool elect_leader(std::string rid, unique_ptr<ConservatorFramework> & framework)
{
    watchTriggered = false;
    std::vector<string> children = framework->getChildren()->forPath("/masters");
    // LOG(INFO) << "number of children " << children.size();
    std::cout<< "number of children " << children.size() << std::endl;

    if (children.size() > 1) 
    {
      std::sort(children.begin(), children.end());
    
    }
    std::string path = "/masters/" + children.at(0);
    std::string id = framework->getData()->forPath(path);
    std::cout << "smallest znode is: " << path << " and id is " << id <<std::endl;
    
    if (id.compare(rid) == 0)
    {
        // you are the leader. create an ephemeral node /leader which will get deleted if you stop
        framework->create()->withFlags(ZOO_EPHEMERAL)->forPath("/leader");
        framework->setData()->forPath("/leader", children.at(0).c_str());
        //framework->setData()->forPath("/masters", children.at(0).c_str());
        LOG(INFO) << " I am the leader " << children.at(0) ;
        std::cout << " I am the leader" <<std::endl;
        return true;
    }
    return false;
}

int main(int argc, char** argv) {
  // Instantiate the client. It requires a channel, out of which the actual RPCs
  // are created. This channel models a connection to an endpoint specified by
  // the argument "--target=" which is the only expected argument.
  // We indicate that the channel isn't authenticated (use of
  // InsecureChannelCredentials()).
  
  FLAGS_log_dir = "/srv/project/logs";
  FLAGS_alsologtostderr = 1;

  google::InitGoogleLogging(argv[0]);

  std::string podid =  std::getenv("MY_POD_NAME");
  std::string ns = std::getenv("MY_POD_NAMESPACE");

  std::cout <<"my pod id is:  " << podid << std::endl;
  //Create factory for framework
  ConservatorFrameworkFactory factory = ConservatorFrameworkFactory();
  //Create a new connection object to Zookeeper
  std::string zkDNS = "zookeeper." + ns + ".svc.cluster.local:2181";
  unique_ptr<ConservatorFramework> framework = factory.newClient(zkDNS,10000000);
  //unique_ptr<ConservatorFramework> framework = factory.newClient("zookeeper.mapreducens.svc.cluster.local:2181",10000000);

  //Start the connection
  framework->start();

  bool isLeader = false;
  bool existLeader = false;

  auto ret = framework->checkExists()->withWatcher(exists_watcher_fn, ((void *) framework.get()))->forPath("/leader");
  if (ret != ZNONODE)
  {
    existLeader = true;
    std::cout<<"leader exists " << std::endl;
  }

  framework->create()->forPath("/masters");
  framework->create()->withFlags(ZOO_SEQUENCE|ZOO_EPHEMERAL)->forPath("/masters/master_", podid.c_str());

  if ( !existLeader )
  {
    isLeader = elect_leader(podid, framework);
  } 

  while(!isLeader) 
  {
      std::this_thread::sleep_for(std::chrono::milliseconds(1000));
      if (watchTriggered)
      {
        isLeader = elect_leader(podid, framework);
      }
  }

  vector<string> wrkrs = framework->getChildren()->forPath("/workers");
  std::string wrkPath = "/workers/" + wrkrs.at(0);
  std::cout << "worker path " << wrkPath << std::endl;

  std::string target_str = framework->getData()->forPath("/workers/worker1") + ":50051";
  std::cout << "ip address of worker" << target_str << std::endl;
  if (target_str.empty())
  {
    std::cout << " cannot retrieve ip address of worker " << std::endl;
  }

  GreeterClient greeter(grpc::CreateChannel(target_str, grpc::InsecureChannelCredentials()));
  std::string user("hello ");

  while (true) {
    // std::this_thread::sleep_for(std::chrono::milliseconds(1000)); // avoid spamming
    std::string reply = greeter.SayHello(user);
    LOG(INFO) << "Greeter received: " << reply;
    std::cout << "Greeter received: " << reply << std::endl;
  }
  google::ShutdownGoogleLogging();
  framework->close();
  return 0;
}