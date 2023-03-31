#pragma once

#include <iostream>
#include <boost/shared_ptr.hpp>
#include <boost/network/protocol/http/server.hpp>
#include <boost/network/utils/thread_pool.hpp>
#include <boost/asio.hpp>

#include <exception>
#include <thread>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <map>
#include "master.h"

struct connection_handler;

typedef boost::network::http::server<connection_handler> server;


///
/// Encapsulates request & connection
///
struct req_handler : std::enable_shared_from_this<req_handler> {
    const server::request&  req;
    server::connection_ptr  conn;

public:
    req_handler(const server::request& req, const server::connection_ptr& conn)
        : req(req)
        , conn(conn) {
        LOG(INFO) << "############ NEW REQUEST ############";
        const std::string dest = destination(req);
        m_container.clear();

        if (dest.find("/upload") != std::string::npos) {
            auto queries = get_queries(dest);
            auto itr = queries.find("container");
            // if (itr != queries.end())
            // {
            //     LOG(INFO) << "container name extracted ";
            //     m_container = itr->second;
            // }
            for(auto itr = queries.begin(); itr != queries.end(); itr++)
            {
                LOG(INFO) << "query parameter: " << itr->first << " value:" << itr->second;
                if (itr->first.compare("container") == 0)
                {
                    m_container = itr->second;
                }
                if (itr->first.compare("m") == 0)
                {
                    m_mappers = itr->second;
                    LOG(INFO) << "number of mappers " << m_mappers;
                }
                if (itr->first.compare("r") == 0)
                {
                    m_reducers = itr->second;
                }
            }
        }
    }
    int request_id()
    {
     static int id_ = -1;

     return ++id_;
    }
    void process(Master * leader)
    {
        leader->set_requestid(request_id());
        //leader->set_num_reducers("2");
        //leader->set_num_mappers("4");
        leader->set_num_reducers(m_reducers);
        leader->set_num_mappers(m_mappers);
        leader->create_container(m_container);


        leader->create_sharding_data();
        leader->get_worker_addresses();

        std::thread t (&Master::start_ping, leader);
        //bool stop_repl_as_leader = false;
        //std::thread r (&Master::start_repl_server, leader, std::ref(stop_repl_as_leader));
        leader->runMapper();
        leader->runReducer();

        //stop_repl_as_leader = true;
        leader->stop_ping();
        t.join();
        //r.join();
    }
    ~req_handler() {

    }


private:
    ///
    /// Parses the string and gets the query as a key-value pair
    ///
    /// @param [in] dest String containing the path and the queries, without the fragment,
    ///                  of the form "/path?key1=value1&key2=value2"
    ///
    std::map<std::string, std::string> get_queries(const std::string dest)
    {

        std::size_t pos = dest.find_first_of("?");

        std::map<std::string, std::string> queries;
        if (pos != std::string::npos) {
            std::string query_string = dest.substr(pos + 1);

            // Replace '&' with space
            for (pos = 0; pos < query_string.size(); pos++) {
                if (query_string[pos] == '&') {
                    query_string[pos] = ' ';
                }
            }

            std::istringstream sin(query_string);
            while (sin >> query_string) {

                pos = query_string.find_first_of("=");

                if (pos != std::string::npos) {
                    const std::string key = query_string.substr(0, pos);
                    const std::string value = query_string.substr(pos + 1);
                    queries[key] = value;
                    std::cout << "query key:" << key << " value:" << value << std::endl;
                }
            }
        }

        return queries;
    }

private:
    std::string m_container;
    std::string m_mappers;
    std::string m_reducers;
};

///
/// Functor that gets executed whenever there is a packet on the HTTP port
///
class connection_handler {
 public:
    connection_handler(Master * m)
        :m_leader(m)
    {
        LOG(INFO) << "instantiated connection handler";
    }
    /// Gets executed whenever there is a packet on the HTTP port.
    ///
    /// @param [in] req Request object that holds the protobuf data
    /// @param [in] conn Connection object
    ///
    void operator()(server::request const& req, const server::connection_ptr& conn)
    {
        std::map<std::string, std::string> headers = {
            {"Connection","close"},
            {"Content-Length", "0"},
            {"Content-Type", "text/plain"}
        };

        const std::string dest = destination(req);

        if (req.method == "POST" && dest.find("/upload") != std::string::npos) {
            try {
                auto start = std::chrono::high_resolution_clock::now();
                // Create a file uploader
                std::shared_ptr<req_handler> rhandler(new req_handler(req, conn));

                rhandler->process(m_leader);

                auto end = std::chrono::high_resolution_clock::now();
                std::chrono::duration<double, std::milli> diff = end - start;
                std::ostringstream stm;
                stm << "Took " << diff.count() << " milliseconds for submitting the job." << std::endl;
                auto body = stm.str();
                // Respond to the client
                headers["Content-Length"] = std::to_string(body.size());
                conn->set_status(server::connection::ok);
                conn->set_headers(headers);
                conn->write(body);
            } catch (const exception& e) {
                const std::string err = e.what();
                headers["Content-Length"] = std::to_string(err.size());
                conn->set_status(server::connection::bad_request);
                conn->set_headers(headers);
                conn->write(err);
            }
        } else {
            static constexpr char body[] = "Only path allowed is /upload";
            headers["Content-Length"] = std::to_string(sizeof(body));
            conn->set_status(server::connection::bad_request);
            conn->set_headers(headers);
            conn->write(body);
        }
    }

 private:
    Master * m_leader;
};

