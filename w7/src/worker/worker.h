#ifndef _WORKER_H
#define _WORKER_H

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
class MapReduceServiceImpl;

#define EXTRA_LEN 16
#define BUFFLEN 512



class WorkerDBConnection
{
  public:
    WorkerDBConnection(const std::string & conn, const std::string & name)
    {
      m_connection = conn;
      m_mstrcontainername = name;
      init();
    }

    std::string get_worker_containername()
    {
      return m_wrkrcontainername ;
    }

    // write the shard data to local temp file
    bool write_partial_data_to_local_file(std::string blobname, size_t begin, size_t end, std::string address, std::string & outfile_name)
    {
        try {
          LOG(INFO)<< "getting shard offset:" << begin << " with blob name " << blobname;

          auto block_blob = m_mstrcontainer.get_block_blob_reference(blobname);
          LOG(INFO) << "before ";
          concurrency::streams::container_buffer<std::vector<uint8_t>> buffer;

          concurrency::streams::ostream out_stream(buffer);
          size_t len = (end- begin ) + EXTRA_LEN;
          LOG(INFO) << "before download";
          block_blob.download_range_to_stream(out_stream, begin, len);
          LOG(INFO) << "downloaded shard offset:" << begin;

          fix_word_boundary(begin, end, buffer.collection());
          LOG(INFO) << "fixed word boundary shard offset:" << begin;
          size_t len_downloaded = buffer.collection().size();

          outfile_name= "sh-"+std::to_string(begin)+"-"+std::to_string(end) + "-" + address;
          std::ofstream out_file(outfile_name, std::ofstream::binary);
          out_file.write(reinterpret_cast<char *>(buffer.collection().data()), len_downloaded);
          out_file.close();
          out_stream.close().wait();
          LOG(INFO) << "done writing local copy "<< outfile_name << " for shard offset:" << begin << " and len:" << (end -begin);
          return true;
        }
        catch (const azure::storage::storage_exception& e)
        {
            LOG(INFO) << "Error: " << e.what() << std::endl;

            azure::storage::request_result result = e.result();
            azure::storage::storage_extended_error extended_error = result.extended_error();
            if (!extended_error.message().empty())
            {
                LOG(INFO) << extended_error.message() << std::endl;
            }
            return false;
        }
        catch (exception e)
        {
          LOG(INFO) << "exception thrown " << e.what();
          return false;
        }
    }
    // // write the intermediate blob data into local temp file
    // bool write_full_data_to_local_file1(std::string blob, std::string containername, std::string address, std::string & outfile_name)
    // {
    //     try
    //     {
    //       auto container = m_blob_client.get_container_reference(containername);
    //       container.create_if_not_exists();

    //       auto append_blob = container.get_append_blob_reference(blob);
    //       concurrency::streams::container_buffer<std::vector<uint8_t>> buffer;

    //       concurrency::streams::ostream out_stream(buffer);

    //       append_blob.download_to_stream(out_stream);
    //       size_t len_downloaded = buffer.collection().size();
    //       outfile_name = "kv-" + address;
    //       std::ofstream out_file(outfile_name, std::ofstream::binary);
    //       out_file.write(reinterpret_cast<char *>(buffer.collection().data()), len_downloaded);
    //       out_file.close();
    //       out_stream.close().wait();
    //       LOG(INFO) << "done writing local copy " << outfile_name;
    //       return true;
    //     }
    //     catch (exception e)
    //     {
    //         return false;
    //     }
    // }

    // write the intermediate blob data into local temp file
    bool write_full_data_to_local_file(std::string containername, std::string blob, std::string address, std::ofstream &tmp_file)
    {
        try
        {
          auto container = m_blob_client.get_container_reference(containername);
          container.create_if_not_exists();

          auto block_blob = container.get_block_blob_reference(blob);
          concurrency::streams::container_buffer<std::vector<uint8_t>> buffer;

          concurrency::streams::ostream out_stream(buffer);

          block_blob.download_to_stream(out_stream);
          size_t len_downloaded = buffer.collection().size();

          LOG(INFO) << "writing temporary local copy of blob:" << blob << " containername:" << containername;
          //std::ofstream out_file;
          //out_file.open(outfile_name, std::ios_base::app | std::ios::binary);
          //std::ofstream out_file(outfile_name, std::ofstream::binary);
          tmp_file.write(reinterpret_cast<char *>(buffer.collection().data()), len_downloaded);
          //tm_file.close();
          //out_stream.close().wait();
          LOG(INFO) << "done writing temporary local copy";
          return true;
        }
        catch (exception e)
        {
            return false;
        }
    }
    // create append blob inside 'int' container
    void append_to_blob(int blob_id)
    {
      std::string outfile_name = "int_" + std::to_string(blob_id);
      auto blob = m_wrkrcontainer.get_append_blob_reference(outfile_name);
      LOG(INFO) <<"append blob created ";
      concurrency::streams::istream input_stream = concurrency::streams::file_stream<uint8_t>::open_istream(outfile_name).get();
      blob .upload_from_stream(input_stream);
      LOG(INFO) <<"uploading from "<< outfile_name << " stream done ";
      input_stream.close().wait();
    }

    // create block blob inside 'int' container
    void add_intermediate_blob(int reducer_id, std::string address, int sid, int spid, std::string & containername, std::vector<std::string>& bloblst)
    {
      containername = m_wrkrcontainername;
      std::string blobname;
      blobname.clear();
      blobname = "int_" + address + '_' + std::to_string(sid) + '_' + std::to_string(spid) + '_' + std::to_string(reducer_id);


      auto block_blob = m_wrkrcontainer.get_block_blob_reference(blobname);
      LOG(INFO) << "block blob created " << blobname;
      concurrency::streams::istream input_stream = concurrency::streams::file_stream<uint8_t>::open_istream(blobname).get();
      block_blob.upload_from_stream(input_stream);
      LOG(INFO) << "uploading from "<< blobname << " stream done ";
      input_stream.close().wait();
      bloblst.push_back(blobname);
    }

    // create 'out' container and a blob 'out_<wrkr address>
    void add_out_blob(std::string outfile_name, std::string & blob, std::string & containername)
    {
      containername = m_mstrcontainername + "out";
      auto container = m_blob_client.get_container_reference(containername);
      container.create_if_not_exists();

      blob = outfile_name;
      auto block_blob = container.get_block_blob_reference(blob);
      LOG(INFO) << "block blob created " << blob;

      concurrency::streams::istream input_stream = concurrency::streams::file_stream<uint8_t>::open_istream(outfile_name).get();
      block_blob.upload_from_stream(input_stream);
      LOG(INFO) <<"uploading from "<< outfile_name << " stream done ";
      input_stream.close().wait();
    }

  private:

    void fix_word_boundary(size_t begin, size_t end, std::vector<uint8_t> & collection)
    {
      if (begin != 0)
      {
          int i = 0;
          while (i < EXTRA_LEN)
          {
              char c = collection.at(i);
              if (c == ' ' || c == '.' || c ==',') break;
              collection[i] = ' ';
              i++;
          }

      }
      int endpos = end - begin;
      int eofpos = collection.size();
      if (eofpos > endpos)
      {
          LOG(INFO) << "fixed the end of the shard ";
          int i = endpos;
          while (i < collection.size())
          {
              char c = collection.at(i);
              if (c == ' ' || c == '.' || c ==',') break;
              i++;
          }

          collection.erase(collection.begin() + i, collection.end());
          // add_suffix(collection);

      }
      else
      {
          // add_suffix(collection);

      }
    }
    void add_suffix(std::vector<uint8_t> & collection)
    {

        collection.push_back(' ');
        collection.push_back('g');
        collection.push_back('a');
        collection.push_back('t');
        collection.push_back('e');
        collection.push_back('c');
        collection.push_back('h');
    }

    void init ()
    {
      try
      {
        auto st_account = azure::storage::cloud_storage_account::parse(m_connection);
        LOG(INFO) << "mr19 storage account connected";

        m_blob_client = st_account.create_cloud_blob_client();
        LOG(INFO) << "blob client is created";

        m_mstrcontainer = m_blob_client.get_container_reference(m_mstrcontainername);
        m_mstrcontainer.create_if_not_exists();

        m_wrkrcontainername = m_mstrcontainername + "int";
        LOG(INFO) << "creating container "<< m_wrkrcontainername << " with blob client";
        m_wrkrcontainer = m_blob_client.get_container_reference(m_wrkrcontainername);
        m_wrkrcontainer.create_if_not_exists();
        LOG(INFO) << "created container "<< m_wrkrcontainername << " with blob client";
      }
      catch (const azure::storage::storage_exception& e)
      {
          LOG(INFO) << "Error: " << e.what() << std::endl;

          azure::storage::request_result result = e.result();
          azure::storage::storage_extended_error extended_error = result.extended_error();
          if (!extended_error.message().empty())
          {
              LOG(INFO) << extended_error.message() << std::endl;
          }
      }
      catch (exception e)
      {
        LOG(INFO) << "exception thrown " << e.what();
      }

    }

  private:

    std::string m_connection;
    std::string m_mstrcontainername;
    std::string m_wrkrcontainername;
    azure::storage::cloud_blob_client m_blob_client;
    azure::storage::cloud_blob_container m_mstrcontainer;
    azure::storage::cloud_blob_container m_wrkrcontainer;
};








class Worker
{
  public:
    Worker(int fail_after);

    ~Worker();

    void run();
    bool is_alive();

    std::string get_address();
    std::string get_intermediate_container();
    void create_connection(const std::string & connString, const std::string & containerName);
    bool map_shard(std::string blobname, size_t begin, size_t end, int nReducers, int sid, int spid, std::string & out_container, std::vector<std::string> &bloblst);

    void prepare_data(std::string in_container, std::string in_blob, std::ofstream & tmp_file);
    bool reduce(std::string in_container, std::string kvfilename, int reducer_id, std::string & out_blob, std::string & out_container);

    void task_assigned_check_fail();

  private:
    void instantiate_map_function(std::string fname);

    void instantiate_reduce_function(std::string fname);

    void instantiate_py_func(std::string script, std::string funcname, std::string filename);

    void stream_to_file(const char * word, long val, bool ismap);

    void create_r_intermediate_files(int sid, int spid);

    void close_r_intermediate_files();

    void append_intermediate_file_to_blob();

    void add_intermediate_file_to_blob(int sid, int spid, std::string & container, std::vector<std::string> & bloblst);

    void create_output_file(int reducer_id);
    void close_output_file();

    void add_out_file_to_blob(std::string &blob, std::string & container);

    void enter_fail_state();

    void connect_to_zk();
    void register_with_zk();
    void set_address_info();
    int choose_local_port();
    void register_address_with_zk();
    void run_server();

  private:
    int m_port;
    std::string m_ip;
    std::string m_full_address;
    std::string m_nodepath;

    unique_ptr<ConservatorFramework> m_framework; // Zookeeper
    WorkerDBConnection * m_dbConnection; // Azure
    MapReduceServiceImpl * m_server; // gRPC

    bool m_has_failed;
    bool m_will_fail;
    int m_until_fail;

    int m_nreducers;
    PyObject *m_pyMapFunc;
    PyObject *m_pyReduceFunc;

    std::ofstream m_outstream;
    std::string m_outfile;
    std::vector<std::ofstream> m_inter_files;

};

#endif
