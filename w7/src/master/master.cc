#include <glog/logging.h>
#include "master.h"

#include <dirent.h>
#include <sys/stat.h>


void start_logging(char* name) {
  // Set flags
  FLAGS_log_dir = "logs";
  FLAGS_alsologtostderr = 1;
  FLAGS_logbufsecs = 1; // default is 30
  FLAGS_logbuflevel = -1; // Don't buffer INFO

  // Create logging directory
  if (opendir(FLAGS_log_dir.c_str()) ==  nullptr)
  {
    mkdir(FLAGS_log_dir.c_str(), S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
  }

  // Start
  google::InitGoogleLogging(name);
}
void stop_logging() {
  google::ShutdownGoogleLogging();
}

int main(int argc, char** argv) {
  start_logging(argv[0]);

  int fail_after = (argc == 1) ? -1 : std::stoi(argv[1]);
  Master master(fail_after);
  master.run();

  stop_logging();
  return 0;
}