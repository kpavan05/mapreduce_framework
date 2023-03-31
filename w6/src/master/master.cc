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

int main(int argc, char** argv) {

  FLAGS_log_dir = "/srv/project/logs";
  FLAGS_alsologtostderr = 1;
  FLAGS_logbufsecs = 5; // default is 30
  google::InitGoogleLogging(argv[0]);

  std::string podid =  std::getenv("MY_POD_NAME");
  std::string ns = std::getenv("MY_POD_NAMESPACE");

  std::cout << "my pod id is:  " << podid << std::endl;

  //Create factory for framework
  ConservatorFrameworkFactory factory = ConservatorFrameworkFactory();
  //Create a new connection object to Zookeeper
  unique_ptr<ConservatorFramework> framework = factory.newClient("zookeeper.mr.svc.cluster.local:2181",10000000);
  //Start the connection
  framework->start();

  std::string ownPath = "";
  framework->create()->forPath("/masters");
  framework->create()->withFlags(ZOO_SEQUENCE|ZOO_EPHEMERAL)->forPath("/masters/master_", NULL, ownPath);
  LOG(INFO) << "I am " << ownPath;
  std::cout << "I am " << ownPath << std::endl;

  std::vector<string> children = framework->getChildren()->forPath("/masters");
  LOG(INFO) << "number of children " << children.size(); // Just to see if both masters started before one identified itself as leader
  std::cout << "number of children " << children.size() << std::endl;

  // Become leader if: (leader is not set or leader does not exist) and (this instance has the lowest id of existing masters)
  // Otherwise: sleep & keep checking
  bool amLeader = false;
  bool hadPreviousLeader = false;
  while (!amLeader) {
    std::string curLeader = framework->getData()->forPath("/masters");
    if (curLeader != "") {
      if (!hadPreviousLeader) {
         LOG(INFO) << "I am not the leader. Now waiting...";
         std::cout << "I am not the leader. Now waiting..." << std::endl;
      }
      hadPreviousLeader = true;
    } 
    if (
      (curLeader != "") &&
      (framework->checkExists()->forPath(curLeader) == ZOK) // Could use a watch instead?
    ) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1000));
      continue;
    }
    // No current leader

    // Determine next leader
    std::vector<string> children = framework->getChildren()->forPath("/masters");
    // std::cout << "number of children " << children.size() << std::endl;
    std::sort(children.begin(), children.end());
    std::string nextLeader = "/masters/" + children.at(0);

    if (nextLeader != ownPath) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1000));
      continue;
    }
    // Become the new leader
    framework->setData()->forPath("/masters", ownPath.c_str());
    amLeader = true;
  }

  if (!hadPreviousLeader) {
    LOG(INFO) << "I am now the first leader.";
    std::cout << "I am now the first leader." << std::endl;
  } else {
    LOG(INFO) << "I am now replacing a previous leader.";
    std::cout << "I am now replacing a previous leader." << std::endl;
  }

  vector<string> wrkrs = framework->getChildren()->forPath("/workers");
  std::string wrkPath = "/workers/" + wrkrs.at(0);
  std::cout << "worker path " << wrkPath << std::endl;

  std::string target_str = framework->getData()->forPath(wrkPath) + ":50051";
  while (target_str.empty()) // This check might not be working
  {
    LOG(INFO) << " cannot retrieve ip address of worker ";
    std::cout << " cannot retrieve ip address of worker " << std::endl;

    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    std::string target_str = framework->getData()->forPath(wrkPath) + ":50051";
  }
  LOG(INFO) << "ip address of worker: " << target_str;
  std::cout << "ip address of worker: " << target_str << std::endl;

  GreeterClient greeter(grpc::CreateChannel(target_str, grpc::InsecureChannelCredentials()));
  std::string user("hello ");

  while (true) {
    std::this_thread::sleep_for(std::chrono::milliseconds(2000)); // avoid spamming
    std::string reply = greeter.SayHello(user);
    LOG(INFO) << "Greeter received: " << reply; 
    std::cout << "Greeter received: " << reply << std::endl;
  }

  google::ShutdownGoogleLogging();
  framework->close();
  return 0;
}
