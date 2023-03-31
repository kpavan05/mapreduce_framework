#include "worker.h"
#include "mrservice.h"

#include <Python.h>
#include <iostream>
#include <memory>
#include <string>
#include <vector>
#include <exception>
#include <glog/logging.h>

#include <was/storage_account.h>
#include <was/blob.h>
#include <cpprest/filestream.h>
#include <cpprest/containerstream.h>
#include <conservator/ConservatorFrameworkFactory.h>
#include <zookeeper/zookeeper.h>


Worker::Worker(int fail_after)
{
  m_port = 0;
  m_ip = "";
  m_full_address = "";
  m_nodepath = "";

  m_framework = nullptr;
  m_dbConnection = nullptr;
  m_server = nullptr;

  m_has_failed = false;
  if (fail_after <= 0)
  {
    m_will_fail = false;
    m_until_fail = 0;
    LOG(INFO) << "This worker is not assigned to fail";
  } else
  {
    m_will_fail = true;
    m_until_fail = fail_after;
    LOG(INFO) << "This worker will fail after completing " << std::to_string(m_until_fail) << " tasks";
  }

  m_nreducers = 0;
  Py_Initialize();
  PyRun_SimpleString("import sys");
  PyRun_SimpleString("sys.path.append(\".\")");

  m_outstream.clear();
  m_outfile.clear();
  m_inter_files.clear();
}

Worker::~Worker()
{
  Py_FinalizeEx();
  m_framework->close();
  m_server->~MapReduceServiceImpl();
}

void Worker::run() {
  connect_to_zk();
  register_with_zk();
  set_address_info();
  register_address_with_zk();
  run_server();
}

bool Worker::is_alive()
{
  return !m_has_failed;
}

std::string Worker::get_address() { return m_full_address; }
std::string Worker::get_intermediate_container()
{
  return m_dbConnection->get_worker_containername();
}

void Worker::create_connection(const std::string & connString, const std::string & containerName)
{
  m_dbConnection = new WorkerDBConnection(connString, containerName);
}

bool Worker::map_shard(std::string blobname, size_t begin, size_t end, int nReducers, int sid, int spid, std::string & out_container, std::vector<std::string> &bloblst)
{
  std::string outfile;

  m_nreducers = nReducers;
  // create streams for local intermediate files
  create_r_intermediate_files(sid, spid);
  // write blob data to temp file on disk
  m_dbConnection->write_partial_data_to_local_file(blobname, begin, end, m_full_address, outfile);
  LOG(INFO) << " mapper will run on the file " << outfile;
  // run user defined map function and stream to intermediate file
  // call_py(outfile, true);
  instantiate_map_function(outfile);
  // close intermediate file streams
  close_r_intermediate_files();
  // write intermediate file to append blob
  //append_intermediate_file_to_blob();

  add_intermediate_file_to_blob(sid, spid, out_container, bloblst);
  return true;
}

void Worker::prepare_data(std::string in_container, std::string in_blob, std::ofstream & tmp_file)
{
  // write blob data to temp file on disk
  m_dbConnection->write_full_data_to_local_file(in_container, in_blob, m_full_address, tmp_file);
}
bool Worker::reduce(std::string in_container, std::string kvfilename, int reducer_id, std::string & out_blob, std::string & out_container)
{
  out_blob.clear();
  out_container.clear();

  // create stream for output
  create_output_file(reducer_id);
  // write blob data to temp file on disk
  // m_dbConnection->write_full_data_to_local_file(in_blob, in_container, m_full_address, kvfilename);
  LOG(INFO) << " reducer will run on the file " << kvfilename;
  // run user defined reduce function and stream to output stream
  instantiate_reduce_function(kvfilename);
  // close the output stream
  close_output_file();
  // write the final data in output stream to final blob
  add_out_file_to_blob(out_blob, out_container);

  return true;
}

void Worker::task_assigned_check_fail() {
  if (m_will_fail) {
    m_until_fail--;
    if (m_until_fail <= 0) {
      enter_fail_state();
    }
    LOG(INFO) << std::to_string(m_until_fail) << " tasks until failure";
  }
}


void Worker::instantiate_map_function(std::string fname)
{
  LOG(INFO) << "instantiating map python script";
  instantiate_py_func("map" , "mapfunc", fname);
}

void Worker::instantiate_reduce_function(std::string fname)
{
  LOG(INFO) << "instantiating reduce python script";
  instantiate_py_func("reduce", "reducefunc", fname);
}

void Worker::instantiate_py_func(std::string script, std::string funcname, std::string filename)
{
  PyObject *pName, *pModule;
  PyObject *pFunc;
  bool ismap = false;
  if (script.compare("map") == 0)
  {
    ismap = true;
  }
  pName = PyUnicode_DecodeFSDefault(script.c_str());
  pModule = PyImport_Import(pName);
  Py_DECREF(pName);
  LOG(INFO) << "import python module " << script;
  if (pModule != NULL)
  {

        pFunc = PyObject_GetAttrString(pModule, funcname.c_str());
        LOG(INFO) << "created python function " << funcname;
        if (!pFunc || !PyCallable_Check(pFunc))
        {

            Py_DECREF(pFunc);
            Py_DECREF(pModule);
            PyErr_Print();
            LOG(INFO) << "Call to python map function failed";
            pFunc = nullptr;
            PyErr_Clear();
            return;
        }
        else
        {
          PyObject *pArgs, *pValue;
          size_t val = 0;
          pArgs = PyTuple_New(1);


          pValue = PyUnicode_FromString(filename.c_str());
          LOG(INFO) << "assign arguments to script " << filename ;
          if (!pValue)
          {
              Py_DECREF(pArgs);
              LOG(INFO) << "Arguments to python script cannot be converted";
              return;
          }
          PyTuple_SetItem(pArgs, 0, pValue);

          pValue = PyObject_CallObject(pFunc, pArgs);


          Py_DECREF(pArgs);
          if (pValue != NULL)
          {
            LOG(INFO) << "got return value from python function";
            PyObject *key, *value;
            Py_ssize_t pos = 0;

            while (PyDict_Next(pValue, &pos, &key, &value))
            {
                long ncount = PyLong_AsLong(value);
                // convert object to string in python3
                // https://stackoverflow.com/questions/5356773/python-get-string-representation-of-pyobject
                PyObject* repr = PyObject_Repr(key);
                PyObject* str = PyUnicode_AsEncodedString(repr, "utf-8", "~E~");
                const char *word = PyBytes_AS_STRING(str);
                //LOG(INFO) << " key: " << word << " val:" << ncount << std::endl;
                stream_to_file(word, ncount, ismap);

                Py_XDECREF(repr);
                Py_XDECREF(str);
            }
            LOG(INFO) << "python function call done";
            Py_DECREF(pValue);
          }
          else
          {
            if (PyErr_Occurred() != NULL) {
              LOG(INFO) << "python error printed to console below";
              PyErr_Print();
              PyErr_Clear();
            }
            LOG(INFO) << "no return value from python function";
          }
          return;
        }
  }
  else
  {
      PyErr_Print();
      PyErr_Clear();
      LOG(INFO) << "Cannot find the module in python script";
      return;
  }
}

void Worker::stream_to_file(const char * word, long val, bool ismap)
{
  if (ismap)
  {
    std::string s(word);
    int file_num = (std::hash<std::string>{} (s)) % m_nreducers;
    //LOG(INFO) << "streaming to " << file_num << " key: " << word << " val:" << val << std::endl;
    m_inter_files[file_num] << word << ':' << std::to_string(val) <<std::endl;
  }
  else
  {
    //LOG(INFO) << " key: " << word << " val:" << val << std::endl;
    m_outstream << word << ':' << std::to_string(val) << std::endl;
  }
}

void Worker::create_r_intermediate_files(int sid, int spid)
{

  for (int i = 0; i < m_nreducers; i++)
  {
      std::string iname;
      iname.clear();
      iname = "int_";
      iname += m_full_address;
      iname += '_';
      iname += std::to_string(sid);
      iname += '_';
      iname += std::to_string(spid);
      iname += '_';
      iname += std::to_string(i);

      LOG(INFO) << "creating intermediate file " << iname;
      m_inter_files.emplace_back(std::ofstream{iname, std::ofstream::out | std::ofstream::trunc});
  }
}

void Worker::close_r_intermediate_files()
{
  LOG(INFO) << "closing intermediate files";
  for (int i = 0; i < m_nreducers; i++)
  {
    m_inter_files[i].close();
  }
  m_inter_files.clear();
}

void Worker::append_intermediate_file_to_blob()
{
  LOG(INFO) << "append intermediate file to blob";
  for (int i = 0; i < m_nreducers; i++)
  {
    m_dbConnection->append_to_blob(i);

  }

}

void Worker::add_intermediate_file_to_blob(int sid, int spid, std::string & container, std::vector<std::string> & bloblst)
{
  LOG(INFO) << "add intermediate file to blob";
  for (int i = 0; i < m_nreducers; i++)
  {

    m_dbConnection->add_intermediate_blob(i, m_full_address, sid, spid, container, bloblst);

  }

}

void Worker::create_output_file(int reducer_id)
{
  m_outfile = "out_" + std::to_string(reducer_id); //  + '_' + m_full_address
  m_outstream.open(m_outfile);
}

void Worker::close_output_file()
{
  m_outstream.close();
}

void Worker::add_out_file_to_blob(std::string &blob, std::string & container)
{
  m_dbConnection->add_out_blob(m_outfile, blob, container);
}

void Worker::enter_fail_state() {
  LOG(INFO) << "##### Entering fail state";
  m_has_failed = true;
  m_framework->close(); // Stop ZK
  m_server->~MapReduceServiceImpl(); // Stop gRPC
  LOG(INFO) << "Going to sleep";
  while (true) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
  }
}

void Worker::connect_to_zk() {
  std::string zkURL = "";
  #ifdef LOCAL
    LOG(INFO) << "LOCAL is defined";
    zkURL = "localhost:2181";
  #else
    LOG(INFO) << "LOCAL is not defined";
    std::string ns = std::getenv("MY_POD_NAMESPACE");
    zkURL = "zookeeper." + ns + ".svc.cluster.local:2181";
  #endif

  // Start the connection
  ConservatorFrameworkFactory factory = ConservatorFrameworkFactory();
  m_framework = factory.newClient(zkURL, 10000000);
  m_framework->start();
}
void Worker::register_with_zk() {
  m_framework->create()->forPath("/workers");
  m_framework->create()->withFlags(ZOO_SEQUENCE|ZOO_EPHEMERAL)->forPath("/workers/worker_", NULL, m_nodepath);
  LOG(INFO) << "worker path is " << m_nodepath;

  // std::vector<string> children = framework->getChildren()->forPath("/workers");
}
void Worker::set_address_info() {
  #ifdef LOCAL
    m_ip = "localhost";
    m_port = choose_local_port();
  #else
    m_ip = std::getenv("MY_POD_IP");
    m_port = 50051;
  #endif

  m_full_address = m_ip + ":" + std::to_string(m_port);
  LOG(INFO) << "worker ip address:" << m_ip << " port:" << m_port;
}
int Worker::choose_local_port() {
  LOG(INFO) << "realpath is:" << m_nodepath;
  int idx = m_nodepath.find('_');
  std::string id = m_nodepath.substr(idx + 1);
  LOG(INFO) << "numerical id is :" << id;
  long x = strtol(id.c_str(), nullptr, 10);

  LOG(INFO) << "numerical id is :" << x;
  return 50050 + x;
}
void Worker::register_address_with_zk() {
  m_framework->setData()->forPath(m_nodepath, m_full_address.c_str());
}
void Worker::run_server() {
  m_server = new MapReduceServiceImpl();
  m_server->RunServer(m_ip, m_port, &*this);
}
