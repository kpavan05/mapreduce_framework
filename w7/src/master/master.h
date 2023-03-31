#pragma once

#define LEADER_GRPC_PORT 6060
#define FOLLOWER_GRPC_PORT 6061
#define AZURE_CONNECTION_STRING "DefaultEndpointsProtocol=https;AccountName=mr19;AccountKey=h02uxedr158QS+BbMsrdbuSViOD9qjcTVCH7WPGv3qS260+8uoIrynBoGs0FQ2TKuSy9PyX15sck+ASt7iThbA==;EndpointSuffix=core.windows.net"

#include "helpers.h"
#include "mr.grpc.pb.h"
#include <conservator/ConservatorFrameworkFactory.h>

class MapReduceClient;
class DBConnection;
class ReplicationServiceImpl;
using mr::CheckPointData;


#include <boost/network/protocol/http/server.hpp>
struct connection_handler;
typedef boost::network::http::server<connection_handler> server;


class Master
{
  public:
    Master(int);
    ~Master();
    void run();
    void set_num_mappers(std::string smappers);
    void set_num_reducers(std::string sreducers);
    void add_client(MapReduceClient * client, std::string addr, std::string wrkrPath);
    void set_requestid(int id) ;
    void create_connection(const std::string & conn_string);
    void create_container(std::string container);
    void start_repl_server(ReplicationServiceImpl &r, bool &done);
    void start_repl_client(/*std::string , */bool & done);

    void serialize_data(CheckPointData *);
    void deserialize_data(const CheckPointData * );

    void start_ping();
    void stop_ping();
    void ping_workers();

    void create_sharding_data();

    void get_worker_addresses();

    void runMapper();
    void runReducer();
    bool hasReplicationData() { return m_bReplicated;}
    void processReplicatedData() ;

  private:
    void print_shards();
    bool doesReplMasterExists();
    MapReduceClient* findHealthyClient();
    MapReduceClient* findMinReqHealthyClient(bool bReduce);
    //void dispatchReduceTasks();
    void dispatchReduceTasks(std::queue<int> & );
    void dispatchMapTasks(std::queue<int> & );

    void task_assigned_check_fail();
    void enter_fail_state();

    void update_workers();
    bool worker_client_already_added(std::string wrkPath);

    void init_zk_framework();
    std::string getSrvAddress();
    void register_with_zk();
    void wait_until_leader();
    bool elect_leader();
    void run_as_leader();

  private:
    std::string m_address;

    unique_ptr<ConservatorFramework> m_framework;
    std::string m_nodepath;

    bool m_has_failed;
    bool m_will_fail;
    int m_until_fail;

    std::string m_connection;
    DBConnection *m_dbConnection;

    std::vector<MapReduceClient *> m_Clients;
    std::map<MapReduceClient * , ClientInfo> m_ClientMap;

    bool m_stop_repl_as_leader;
    bool m_stop_repl_as_follower;
    bool m_map_done;

    bool m_hbcheck;
    bool m_bReplicated;

    server *m_server;

    // Job and tasks
    int m_rid;
    int m_nmappers;
    int m_nreducers;
    std::string m_containername;
    size_t m_shardsz;
    std::vector<FileShard> m_shards;
    std::map<int, MapReduceClient *> m_ShardClientMapper;

    // intermediate
    std::map<int, MapperResult> m_IntermediateResultMap;
    std::list<std::string> m_IntblobList;
    std::string m_intcontainer;

    // final output
    std::map<int, std::string> m_OutputResultMap;

    std::thread repl_srv_thd;
    std::thread repl_clt_thd;
    std::thread m_t1;
    std::thread m_r1;
};
