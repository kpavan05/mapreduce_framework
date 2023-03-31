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
#include <vector>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>

#include "worker.h"


void start_logging(char* name) {
  FLAGS_log_dir = "logs";
  FLAGS_alsologtostderr = 1;
  FLAGS_logbufsecs = 1; // default is 30
  FLAGS_logbuflevel = -1; // Don't buffer INFO

  //create logging directory
  if (opendir(FLAGS_log_dir.c_str()) ==  nullptr)
  {
      mkdir(FLAGS_log_dir.c_str(), S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
  }

  google::InitGoogleLogging(name);
  LOG(INFO) << "Logging started";
}
void stop_logging() {
  google::ShutdownGoogleLogging();
}

int main(int argc, char** argv)
{
  start_logging(argv[0]);

  int fail_after = (argc == 1) ? -1 : std::stoi(argv[1]);
  Worker wrkr(fail_after);
  wrkr.run();

  stop_logging();
  return 0;
}