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

#include <unistd.h>
#include<sys/types.h>
#include<sys/socket.h>
#include<sys/un.h>
#include<string.h>
#include<netdb.h>
#include<netinet/in.h>
#include<arpa/inet.h>

#include <iostream>
#include <memory>
#include <string>
#include <ifaddrs.h>
#include <vector>

#include <conservator/ConservatorFrameworkFactory.h>
#include <zookeeper/zookeeper.h>

#include <grpcpp/ext/proto_server_reflection_plugin.h>
#include <grpcpp/grpcpp.h>
#include <grpcpp/health_check_service_interface.h>

#include "sample.grpc.pb.h"
#include <glog/logging.h>
// #define MAX_LEN 512

using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::Status;
using helloworld::Greeter;
using helloworld::HelloReply;
using helloworld::HelloRequest;

// Logic and data behind the server's behavior.
class GreeterServiceImpl final : public Greeter::Service {
  Status SayHello(ServerContext* context, const HelloRequest* request,
                  HelloReply* reply) override {
    std::string suffix("gatech");
    reply->set_message(request->name() + suffix);
    return Status::OK;
  }
};

// No longer used
void GetIPAddress(char* buffer, size_t buflen)
{
    assert(buflen >= 16);
    // create socket connection to a well known server
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    assert(sock != -1);

    const char* knownDnsIp = "8.8.8.8";
    uint16_t knownDnsPort = 53;

    struct sockaddr_in knownServer;
    memset(&knownServer, 0, sizeof(knownServer));
    knownServer.sin_family = AF_INET;
    knownServer.sin_addr.s_addr = inet_addr(knownDnsIp);
    knownServer.sin_port = htons(knownDnsPort);
    // connect to that well known server
    int err = connect(sock, (const sockaddr*) &knownServer, sizeof(knownServer));
    assert(err != -1);

    // now get the connected interface ip address
    struct sockaddr_in name;
    socklen_t namelen = sizeof(name);
    err = getsockname(sock, (sockaddr*) &name, &namelen);
    assert(err != -1);

    const char* p = inet_ntop(AF_INET, &name.sin_addr, buffer, buflen);
    assert(p);

    
    close(sock);
}

void RunServer(std::string ipAddress) {
  
  //std::string server_address("0.0.0.0:50051");
  std::string server_address = ipAddress + ":50051";
  GreeterServiceImpl service;

  grpc::EnableDefaultHealthCheckService(true);
  grpc::reflection::InitProtoReflectionServerBuilderPlugin();
  ServerBuilder builder;
  // Listen on the given address without any authentication mechanism.
  builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
  // Register "service" as the instance through which we'll communicate with
  // clients. In this case it corresponds to an *synchronous* service.
  builder.RegisterService(&service);
  // Finally assemble the server.
  std::unique_ptr<Server> server(builder.BuildAndStart());
  std::cout << "Server listening on " << server_address << std::endl;

  // Wait for the server to shutdown. Note that some other thread must be
  // responsible for shutting down the server for this call to ever return.
  server->Wait();
}

int main(int argc, char** argv) {

  FLAGS_log_dir = "/srv/project/logs";
  FLAGS_alsologtostderr = 1;
  FLAGS_logbufsecs = 5; // default is 30
  google::InitGoogleLogging(argv[0]);

  std::string ipAddress=  std::getenv("MY_POD_IP");
  std::string ns = std::getenv("MY_POD_NAMESPACE");
  std::cout << "ip address of worker is " << ipAddress << std::endl;

  //Create factory for framework
  ConservatorFrameworkFactory factory = ConservatorFrameworkFactory();
  //Create a new connection object to Zookeeper
  unique_ptr<ConservatorFramework> framework = factory.newClient("zookeeper.mr.svc.cluster.local:2181",10000000);
  //Start the connection
  framework->start();

  std::string ownPath = "";
  framework->create()->forPath("/workers");
  framework->create()->withFlags(ZOO_SEQUENCE|ZOO_EPHEMERAL)->forPath("/workers/worker_", NULL, ownPath);
  std::vector<string> children = framework->getChildren()->forPath("/workers");
  std::cout<< "number of children " << children.size() << std::endl;

  framework->setData()->forPath(ownPath, ipAddress.c_str());
  std::string ipAddrData = framework->getData()->forPath(ownPath);
  std::cout << "ip address of worker" << ipAddrData << std::endl;
  RunServer(ipAddress);

  google::ShutdownGoogleLogging();
  framework->close();
  return 0;
}
