#include "gateway_handler.hpp"
#include <cstddef>
#include <fstream>
#include <memory>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include "src/util/util.hpp"
#include "libs/bst-http/http_client_async.hpp"
#include <boost/asio.hpp>

namespace drlog {
    namespace beast = boost::beast;
    namespace http = beast::http;
    namespace net = boost::asio;
    using tcp = net::ip::tcp;

    #define AGENT_TIMEOUT_SECONDS 60

    GTHandler::GTHandler(std::shared_ptr<AgentManager> agent_manager)
        : agent_manager_(agent_manager) {
    }   

    GTHandler::~GTHandler() {}

    net::awaitable<void> GTHandler::hello(std::shared_ptr<http::request<http::string_body>> req,
                std::shared_ptr<http::response<http::string_body>> res,
                std::shared_ptr<bst::request_context> ctx) {
            res->body() = "Hello!!!";
            res->set(http::field::content_type, "text/plain");
            res->result(http::status::ok);
            res->prepare_payload();
            co_return;
    }


    net::awaitable<void> GTHandler::announce(std::shared_ptr<http::request<http::string_body>> req,
            std::shared_ptr<http::response<http::string_body>> res,
            std::shared_ptr<bst::request_context> ctx) {
        try {
            if (req->method() != http::verb::get) {
                res->result(http::status::method_not_allowed);
                spdlog::warn("Method not allowed, only GET is allowed, url: {}",   std::string(req->target()));      
                co_return;
            }
            auto agent_addr = ctx->get_param("agent_addr");
            if (agent_addr.empty()) {
                res->result(http::status::bad_request);
                spdlog::warn("Path parameter agent_addr is required, url: {}", std::string(req->target()));      
                co_return;
            }
            // url decode use boost::asio
            agent_manager_->add_agent(util::url_decode(agent_addr));
            spdlog::debug("Agent announced: {}", util::url_decode(agent_addr));
            res->result(http::status::ok);
            co_return;
        } catch (const std::exception& e) {
            // best-effort 500
            res->result(http::status::internal_server_error);
            spdlog::error("Internal server error in announce handler: {}", e.what());
            co_return;
        } catch (...) {
            // best-effort 500
            res->result(http::status::internal_server_error);
            spdlog::error("Unknown internal server error in announce handler");
            co_return;
            
        }
    }

    net::awaitable<void> GTHandler::web(std::shared_ptr<http::request<http::string_body>> req,
            std::shared_ptr<http::response<http::string_body>> res,
            std::shared_ptr<bst::request_context> ctx) {
        try {
            if (req->method() != http::verb::get) {
                res->result(http::status::method_not_allowed);
                spdlog::warn("Method not allowed, only GET is allowed, url: {}", std::string(req->target()));      
                co_return;
            }

            auto prefix = util::url_decode(ctx->get_param("prefix"));
            if (prefix.empty()) {
                res->result(http::status::bad_request);
                spdlog::warn("Path parameter is required, url: {}", std::string(req->target()));      
                co_return;
            }
            //load html file from path
            auto web_file = web_path_ + "drlog-search.html";
            std::ifstream file(web_file);
            if (!file.is_open()) {
                res->result(http::status::internal_server_error);
                spdlog::error("Failed to open web file: {}", web_file);
                co_return;
            }
            // read file content
            std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
            file.close();
            // set response body
            res->body() = content;
            res->set(http::field::content_type, "text/html");
            res->result(http::status::ok);
            res->prepare_payload();
            co_return;
            
        } catch (const std::exception& e) {
            // best-effort 500
            res->result(http::status::internal_server_error);
            spdlog::error("Internal server error in web handler: {}", e.what());
            co_return;
        } catch (...) {
            // best-effort 500
            res->result(http::status::internal_server_error);
            spdlog::error("Unknown internal server error in web handler");
            co_return;
        }
    }

    net::awaitable<void> GTHandler::agent_list(std::shared_ptr<http::request<http::string_body>> req,
            std::shared_ptr<http::response<http::string_body>> res,
            std::shared_ptr<bst::request_context> ctx) {
        try {
            if (req->method() != http::verb::get) {
                res->result(http::status::method_not_allowed);
                spdlog::warn("Method not allowed, only GET is allowed, url: {}", std::string(req->target()));      
                co_return;
            }
            std::vector<std::shared_ptr<agent_info>> agents = agent_manager_->get_active_agents();
            // json body
            nlohmann::json jbody = nlohmann::json::array();
            for (const auto& agent : agents) {
                nlohmann::json item;
                item["agent_id"] = agent->agent_id;
                item["address"] = agent->address;
                item["last_announce"] = agent->last_announce;
                jbody.push_back(item);
            }
            res->body() = jbody.dump();
            res->set(http::field::content_type, "application/json");
            res->result(http::status::ok);
            res->prepare_payload();
            co_return;
        } catch (const std::exception& e) {
            // best-effort 500
            res->result(http::status::internal_server_error);
            spdlog::error("Internal server error in agent_list handler: {}", e.what());
            co_return;
        } catch (...) {
            // best-effort 500
            res->result(http::status::internal_server_error);
            spdlog::error("Unknown internal server error in agent_list handler");
            co_return;
        }
    }

    net::awaitable<void> GTHandler::list(std::shared_ptr<http::request<http::string_body>> req,
            std::shared_ptr<http::response<http::string_body>> res,
            std::shared_ptr<bst::request_context> ctx) {
        try {
            if (req->method() != http::verb::get) {
                res->result(http::status::method_not_allowed);
                spdlog::warn("Method not allowed, only GET is allowed, url: {}", std::string(req->target()));      
                co_return;
            }

            auto prefix = util::url_decode(ctx->get_param("prefix"));
            if (prefix.empty()) {
                res->result(http::status::bad_request);
                spdlog::warn("Path parameter is required, url: {}", std::string(req->target()));      
                co_return;
            }
            std::vector<std::shared_ptr<agent_info>> agents = agent_manager_->get_active_agents();
            if (agents.empty()) {
                res->result(http::status::service_unavailable);
                spdlog::warn("No active agents available to serve the request, url: {}", std::string(req->target()));      
                co_return;
            }
            // json body
            nlohmann::json jbody = nlohmann::json::array();
            std::vector<agent_file_index> out_indexes;
            co_await get_agent_log_lists(prefix,agents,out_indexes,ctx);
            if (out_indexes.empty()) {
                res->result(http::status::not_found);
                spdlog::warn("No files found for prefix: {}, url: {}", prefix, std::string(req->target()));      
                co_return;
            }
            for (const auto& fi : out_indexes) {
                nlohmann::json item;
                item["path"] = fi.path;
                item["size"] = fi.size;
                item["mtime"] = fi.mtime;
                item["etag"] = fi.etag;
                item["start_time"] = fi.start_time;
                item["end_time"] = fi.end_time;
                item["agent_id"] = fi.agent_id;
                jbody.push_back(item);
            }
            res->body() = jbody.dump();
            res->set(http::field::content_type, "application/json");
            res->result(http::status::ok);
            res->prepare_payload();
            spdlog::debug("List request served for prefix: {}, url: {}", prefix, std::string(req->target()));
            co_return;
        } catch (const std::exception& e) {
            // best-effort 500
            res->result(http::status::internal_server_error);
            spdlog::error("Internal server error in list handler: {}", e.what());
            co_return;
        } catch (...) {
            // best-effort 500
            res->result(http::status::internal_server_error);
            spdlog::error("Unknown internal server error in list handler");
            co_return;
        }
    }

    net::awaitable<void> GTHandler::search(std::shared_ptr<http::request<http::string_body>> req,
            std::shared_ptr<http::response<http::string_body>> res,
            std::shared_ptr<bst::request_context> ctx) {
        try {
            if (req->method() != http::verb::post) {
                res->result(http::status::method_not_allowed);
                spdlog::warn("Method not allowed, only POST is allowed, url: {}", std::string(req->target()));      
                co_return;
            }
            //check path parameter
            auto prefix = util::url_decode(ctx->get_param("prefix"));
            if (prefix.empty()) {
                res->result(http::status::bad_request);
                spdlog::warn("Path parameter is required, url: {}", std::string(req->target()));      
                co_return;
            }
            //load from post body json
            /*{
                "querys": [{"query":"query1","type":"simple"},{"query":"query1","type":"boolean"}],
                "start_time":1761490100,
                "end_time":1761590800,
                "max_results":1000
            }*/
            std::string &body = req->body();
            nlohmann::json jbody = nlohmann::json::parse(body, nullptr, false);
            if (jbody.is_discarded()) {
                res->result(http::status::bad_request);
                spdlog::warn("Invalid JSON format in request body, url: {}", std::string(req->target()));      
                co_return;
            }
            if(!jbody.contains("querys") || !jbody["querys"].is_array()) {
                res->result(http::status::bad_request);
                spdlog::warn("Invalid JSON format in request body, url: {}", std::string(req->target()));      
                co_return;
            }
            if(!jbody.contains("start_time") || !jbody["start_time"].is_number()) {
                res->result(http::status::bad_request);
                spdlog::warn("Invalid JSON format in request body, url: {}", std::string(req->target()));      
                co_return;
            }
            if(!jbody.contains("end_time") || !jbody["end_time"].is_number()) {
                res->result(http::status::bad_request);
                spdlog::warn("Invalid JSON format in request body, url: {}", std::string(req->target()));      
                co_return;
            }
            //get agent list
            auto vec_agents = agent_manager_->get_active_agents();
            ctx->set<std::shared_ptr<std::vector<std::shared_ptr<agent_info>>>>("agents",std::make_shared<std::vector<std::shared_ptr<agent_info>>>(std::move(vec_agents)));
            auto p_agents = *ctx->get<std::shared_ptr<std::vector<std::shared_ptr<agent_info>>>>("agents");
            auto &agents = *p_agents.get();
            if (agents.empty()) {
                res->result(http::status::service_unavailable);
                spdlog::warn("No active agents available to serve the request, url: {}", std::string(req->target()));      
                co_return;
            }
            //get agent log list
            ctx->set<std::shared_ptr<std::vector<agent_file_index>>>("indexes",std::make_shared<std::vector<agent_file_index>>());
            auto p_indexes = ctx->get<std::shared_ptr<std::vector<agent_file_index>>>("indexes");
            auto &indexes = *p_indexes->get();
            co_await get_agent_log_lists(prefix,agents,indexes,ctx);
            if (indexes.empty()) {
                res->result(http::status::not_found);
                spdlog::warn("No files found for prefix: {}, url: {}", prefix, std::string(req->target()));      
                co_return;
            }
            co_await get_agent_search(prefix,jbody,ctx);
            //get results
            auto p_agent_records = ctx->get<std::shared_ptr<std::map<std::string,std::string>>>("agent_records");
            auto &agent_records = *p_agent_records->get();
            //parse records to json
            //output
            //{"status":0,"error_msg":"",
            //"records":[{"agent":"agent1","path":"/path/xxx.log","status":0,"error_msg":"","start_time":0,"end_time":0,"lines":[{"line":"","time":0},{...}]}]}
            nlohmann::json jres;
            jres["status"] = 0;
            jres["error_msg"] = "";
            jres["records"] = nlohmann::json::array();
            for(auto &agent_record : agent_records) {
                nlohmann::json jfm = nlohmann::json::parse(agent_record.second, nullptr, false);
                if (jfm.is_discarded()) {
                    spdlog::warn("Invalid JSON format in record: {} : {}", agent_record.first, agent_record.second);
                    continue;
                }
                if(!jfm["status"].is_number() || jfm["status"].get<int>() != 0) {
                    spdlog::warn("Invalid status in record: {} : {}", agent_record.first, agent_record.second);
                    continue;
                }
                if(!jfm.contains("records") || !jfm["records"].is_array()) {
                    spdlog::warn("Invalid records in record: {} : {}", agent_record.first, agent_record.second);
                    continue;
                }
                for(auto &record : jfm["records"]) {
                    record["agent"] = agent_record.first;
                    jres["records"].push_back(record);
                }
            }
            //sort by jres["records"][i]["start_time"]
            std::sort(jres["records"].begin(),jres["records"].end(),[](const nlohmann::json &a,const nlohmann::json &b) {
                if(!a.contains("start_time") || !a["start_time"].is_number()) {
                    return true;
                }
                if(!b.contains("start_time") || !b["start_time"].is_number()) {
                    return false;
                }
                return a["start_time"].get<int64_t>() < b["start_time"].get<int64_t>();
            });
            //gzip compress if needed
            std::string res_body_j = jres.dump();
            std::string res_body_c;
            std::string content_encoding;
            auto& headers = req->base();
            auto it = headers.find(boost::beast::http::field::accept_encoding);
            if (it != headers.end()) {
                auto accept_encoding = it->value();
                compress_body(res_body_j,accept_encoding,res_body_c,content_encoding);
            }
            //set body
            if(content_encoding == "gzip") {
                res->set(http::field::content_encoding, "gzip");
                res->body() = std::move(res_body_c);
            } else {
                res->body() = std::move(res_body_j);
            }
            res->set(http::field::content_type, "application/json");
            res->prepare_payload();
            spdlog::info("Search completed with {} file matches under request : {}", jres["records"].size(), req->target());
            co_return;
        } catch (const std::exception& e) {
            // best-effort 500
            res->result(http::status::internal_server_error);
            spdlog::error("Internal server error in search handler: {}", e.what());
            co_return;

        } catch (...) {
            // best-effort 500
            res->result(http::status::internal_server_error);
            spdlog::error("Unknown internal server error in search handler");
            co_return;
        }
    }

    void GTHandler::compress_body(const std::string& input,const std::string& accept_encoding, 
            std::string& output,std::string& content_encoding) {
        if(input.size() < 1024) {
            content_encoding = "";
            return;
        }
        if(accept_encoding.find("gzip") != std::string::npos) {
            //gzip compress
            if(util::gzip_compress(input,output)) {
                content_encoding = "gzip";
                return;
            }
            else {
                content_encoding = "";
                return;
            }
        }
    }

    void GTHandler::decompress_body(const std::string& input,const std::string& content_encoding, 
            std::string& output) {
        if(content_encoding.find("gzip") != std::string::npos) {
            //gzip decompress
            if(util::gzip_decompress(input,output)) {
                return;
            }
            else {
                output = "";
                return;
            }
        }
    }

    net::awaitable<void> GTHandler::get_agent_log_lists(const std::string &prefix, std::vector<std::shared_ptr<agent_info>> agents,
        std::vector<agent_file_index>& out_indexes, std::shared_ptr<bst::request_context> ctx) {
        auto executor = co_await net::this_coro::executor;
        const size_t max_tasks_per_coroutine = 10;
        size_t total_agents = agents.size();
        size_t num_coroutines = (total_agents + max_tasks_per_coroutine - 1) / max_tasks_per_coroutine;

        std::vector<net::awaitable<void>> coroutines;
        std::mutex out_indexes_mutex; // Mutex to protect out_indexes

        for (size_t i = 0; i < num_coroutines; ++i) {
            size_t start_index = i * max_tasks_per_coroutine;
            size_t end_index = std::min(start_index + max_tasks_per_coroutine, total_agents);

            coroutines.push_back(net::co_spawn(executor,[this, &agents, start_index, end_index, &out_indexes, &out_indexes_mutex, &prefix]() -> net::awaitable<void> {
                for (size_t j = start_index; j < end_index; ++j) {
                    const auto& agent = agents[j];
                    try {
                        std::string url = "http://" + agent->address + "/log/list" + "?prefix=" + prefix;
                        bst::http_client_async client;
                        bst::request req;
                        bst::response res;
                        req.url = url;
                        client.set_request_timeout(10);

                        int status = co_await client.get(req, res);
                        if (status == 200) {
                            nlohmann::json jbody = nlohmann::json::parse(res.body, nullptr, false);
                            if (!jbody.is_discarded() && jbody.is_array()) {
                                for (const auto& item : jbody) {
                                    agent_file_index index;
                                    index.agent_id = agent->agent_id;
                                    index.path = item.value("path", "");
                                    index.size = item.value("size", 0);
                                    index.mtime = item.value("mtime", 0);
                                    index.etag = item.value("etag", "");
                                    index.start_time = item.value("start_time", 0);
                                    index.end_time = item.value("end_time", 0);
                                    {
                                        std::lock_guard<std::mutex> lock(out_indexes_mutex);
                                        out_indexes.push_back(index);
                                    }
                                }
                            }
                        } else {
                            spdlog::warn("Failed to get log list from agent: {}, status: {}", agent->address, status);
                        }
                    } catch (const std::exception& e) {
                        spdlog::error("Error processing agent {}: {}", agent->address, e.what());
                    } catch (...) {
                        spdlog::error("Unknown error processing agent: {}", agent->address);
                    }
                }
                co_return;
            },net::use_awaitable));
        }

        for (auto& coroutine : coroutines) {
            co_await std::move(coroutine);
        }
        co_return;
    }

    net::awaitable<void> GTHandler::get_agent_search(const std::string &prefix,
        const nlohmann::json &jreq_body,std::shared_ptr<bst::request_context> ctx) {
        auto p_indexes = ctx->get<std::shared_ptr<std::vector<agent_file_index>>>("indexes");
        auto &indexes = *p_indexes->get();
        auto p_agents = ctx->get<std::shared_ptr<std::vector<std::shared_ptr<agent_info>>>>("agents");
        auto &agents = *p_agents->get();
        std::map<std::string,std::shared_ptr<agent_info>> agent_map;
        std::vector<std::vector<agent_file_index>> agent_indexes;
        //sort indexes vector by agent_id
        std::sort(indexes.begin(), indexes.end(), [](const agent_file_index& a, const agent_file_index& b) {
            return a.agent_id < b.agent_id;
        });
        // push indexes to agent_indexes vector group by agent_id
        std::string last_agent_id("");
        for (const auto& index : indexes) {
            if (index.agent_id != last_agent_id) {
                last_agent_id = index.agent_id;
                agent_indexes.push_back({});
            }
            agent_indexes.back().push_back(index);
        }
        // agents to map
        for (const auto& agent : agents) {
            agent_map[agent->agent_id] = agent;
        }
        
        ctx->set("agent_records", std::make_shared<std::map<std::string, std::string>>());  // agent_id -> record json str
        auto p_records = ctx->get<std::shared_ptr<std::map<std::string, std::string>>>("agent_records");
        auto &records = *p_records->get();
        //
        size_t total_agents = agent_indexes.size();
        const size_t max_tasks_per_coroutine = 1;
        size_t num_coroutines = (total_agents + max_tasks_per_coroutine - 1) / max_tasks_per_coroutine;
        auto executor = co_await net::this_coro::executor;
        std::vector<net::awaitable<void>> coroutines;
        std::mutex records_mutex; // Mutex to protect records

        for (size_t i = 0; i < num_coroutines; ++i) {
            size_t start_index = i * max_tasks_per_coroutine;
            size_t end_index = std::min(start_index + max_tasks_per_coroutine, total_agents);

            coroutines.push_back(net::co_spawn(executor,[this, &agent_map, &agent_indexes, &jreq_body, start_index, end_index, &records, &records_mutex, &prefix]() -> net::awaitable<void> {
                for (size_t j = start_index; j < end_index; ++j) {
                    const auto& agent_index = agent_indexes[j];
                    if(agent_index.empty()) {
                        continue;
                    }
                    const auto& agent_id = agent_index[0].agent_id;
                    const std::string &addr = agent_map[agent_id]->address;
                    bst::request req;
                    bst::response res;
                    try {
                        std::string url = "http://" + addr + "/log/search" + "?prefix=" + prefix;
                        bst::http_client_async client;
                        req.url = url;
                        client.set_request_timeout(10);
                        //add the index file list to the request body
                        nlohmann::json jreq_body_tmp = jreq_body;
                        jreq_body_tmp["paths"] = nlohmann::json::array();
                        for (const auto& index : agent_index) {
                            jreq_body_tmp["paths"].push_back(index.path);
                        }
                        req.body = jreq_body_tmp.dump();
                        int status = co_await client.post(req, res);
                        if (status != 200) {
                            spdlog::warn("Failed to get log list from agent: {}, status: {}", req.url, status);
                            continue;
                        }
                        //decompress the response body if needed
                        std::string res_body_c;
                        auto it_ce = res.headers.find("Content-Encoding");
                        if(it_ce != res.headers.end()) {
                            //decompress the response body
                            decompress_body(res.body,it_ce->second,res_body_c);
                        } else {
                            res_body_c = res.body;
                        }
                        //add the record to the records map
                        {
                            std::lock_guard<std::mutex> lock(records_mutex);
                            records[agent_id] = std::move(res_body_c);
                        }
                    } catch (const std::exception& e) {
                        spdlog::error("Error processing agent {}: {}", req.url, e.what());
                    } catch (...) {
                        spdlog::error("Unknown error processing agent: {}", req.url);   
                    }
                }
                co_return;
            },net::use_awaitable));
        }

        for (auto& coroutine : coroutines) {
            co_await std::move(coroutine);
        }
        co_return;
    }
    
} // namespace drlog
