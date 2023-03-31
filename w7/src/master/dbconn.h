#pragma once

#include "helpers.h"
#include <exception>
#include <was/storage_account.h>
#include <was/blob.h>

#include <cpprest/filestream.h>
#include <cpprest/containerstream.h>

class DBConnection {
  public:
  DBConnection(const utility::string_t conn)
  {
    m_connection = conn;

    init();
  }

  std::string get_connection_string()
  {
     return m_connection;
  }

  void add_blob(std::string input_file)
  {
    // if (&m_container == nullptr) { // This doesn't work and I don't know why
    //   LOG(INFO) << "container must be set before getting a blob reference";
    // }
    auto block_blob = m_container.get_block_blob_reference(input_file);
    LOG(INFO) << "block blob created";
    concurrency::streams::istream input_stream = concurrency::streams::file_stream<uint8_t>::open_istream(input_file).get();
    block_blob.upload_from_stream(input_stream);
    LOG(INFO) << "uploading from " << input_file << " stream done";
    input_stream.close().wait();

  }

  size_t calc_shard_sz(int nshards)
  {
    float tot_sz = 0;
    azure::storage::list_blob_item_iterator end_of_result;

    // get the blobs and find out total size of data
    auto itr = m_container.list_blobs();
    for (; itr != end_of_result; ++itr)
    {
        LOG(INFO) << "got a blob";

        if (itr->is_blob())
        {
            auto blob = itr->as_blob();
            auto props = blob.properties();

            auto blob_sz = itr->as_blob().properties().size();
            LOG (INFO) << "blob size is " << blob_sz;
            tot_sz += blob_sz;
        }
    }
    // calculate shard size
    size_t shard_sz = ceil(tot_sz/nshards);
    LOG(INFO) << "shard size is " << shard_sz;
    return shard_sz;
  }

  void make_shards(size_t shard_sz, std::list<shard_info> & shard_list)
  {
    auto itr = m_container.list_blobs();
    azure::storage::list_blob_item_iterator end_of_result;
    size_t rem_shard_sz = shard_sz;
    LOG(INFO) << "starting the sharding logic";

    for (; itr != end_of_result; ++itr)
    {
        if (itr->is_blob())
        {
          std::string blobname = itr->as_blob().name();
          auto blob_sz = itr->as_blob().properties().size();
          size_t begin = 0;
          size_t end = rem_shard_sz;

          while (end <= blob_sz)
          {
            LOG(INFO) << "shard info name:" << itr->as_blob().name() << " offsets:" << begin << " , " << end;
            shard_list.emplace_back(itr->as_blob().name(), begin, end, false);

            rem_shard_sz = shard_sz;
            if (end == blob_sz) break;

            begin = end;
            end = begin + shard_sz;
            if (end > blob_sz) break;
          }

          if (end > blob_sz)
          {
            LOG(INFO) << "shard info name:" << itr->as_blob().name() << " offsets:" << begin << " and " << blob_sz;
            shard_list.emplace_back(itr->as_blob().name(), begin, blob_sz, true);
            rem_shard_sz = rem_shard_sz - (blob_sz - begin);
          }
        }
    }
    LOG(INFO) << "number of shard pieces " << shard_list.size();
  }


  void create_container(std::string container)
  {
      auto blob_client = m_account.create_cloud_blob_client();
      m_container = blob_client.get_container_reference(container);
      m_container.create_if_not_exists();
      LOG(INFO) << "container " << container << " created with blob client";
  }

  private:
  void init()
  {
    try {
      m_account = azure::storage::cloud_storage_account::parse(m_connection);
      LOG(INFO) << "Azure mr19 storage account connected";

    }
    catch (std::exception e)
    {
      LOG(INFO) << "exception thrown " << e.what();
    }

  }

  private:
    utility::string_t m_connection;
    azure::storage::cloud_storage_account m_account;
    azure::storage::cloud_blob_container m_container;
};
