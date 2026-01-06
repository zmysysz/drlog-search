#include "agent_handler.hpp"
#include "searcher.hpp"
#include <iostream>
#include <vector>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <zlib.h>
#include "src/util/util.hpp"

namespace drlog {
    namespace beast = boost::beast;
    namespace http = beast::http;
    namespace net = boost::asio;
    using tcp = net::ip::tcp;

    AgentHandler::AgentHandler(std::shared_ptr<FileIndexer> idx)
        : indexer_(idx){}

    AgentHandler::~AgentHandler() {}

    net::awaitable<void> AgentHandler::hello(std::shared_ptr<http::request<http::string_body>> req,
                std::shared_ptr<http::response<http::string_body>> res,
                std::shared_ptr<bst::request_context> ctx) {
            res->body() = "Hello!!!";
            co_return;
    }

    net::awaitable<void> AgentHandler::list(std::shared_ptr<http::request<http::string_body>> req,
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

            auto results = indexer_->list_prefix(prefix);
            if (results.empty()) {
                res->result(http::status::not_found);
                spdlog::warn("No files found under prefix: {}", prefix);      
                co_return;
            }

            // Build JSON array using nlohmann::json
            nlohmann::json j = nlohmann::json::array();
            for (const auto &fi : results) {
                nlohmann::json item;
                item["path"] = fi.fullpath;
                item["size"] = fi.size;
                item["mtime"] = fi.mtime;
                if (!fi.etag.empty()) item["etag"] = fi.etag;
                if (fi.file_index != nullptr && !fi.file_index->time_indexes.empty()) {
                    item["start_time"] = fi.file_index->time_indexes.front().timestamp;
                    item["end_time"] = fi.file_index->time_indexes.back().timestamp;
                }
                j.push_back(item);
            }
            std::string body = j.dump();

            res->set(http::field::content_type, "application/json");
            res->body() = std::move(body);
            res->prepare_payload();
            spdlog::info("Listed {} files under request : {}", results.size(), req->target());
            co_return;
        } catch ( nlohmann::json::exception& e) {
            // best-effort 500
            res->result(http::status::internal_server_error);
            spdlog::error("JSON error in list handler: {}", e.what());
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

    net::awaitable<void> AgentHandler::search(std::shared_ptr<http::request<http::string_body>> req,
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
            auto results = indexer_->list_prefix(prefix);
            if (results.empty()) {
                res->result(http::status::not_found);
                spdlog::warn("No files found under path: {}", prefix, std::string(req->target()));      
                co_return;
            }

            //load from post body json
            /*{
                "paths": ["path1","path1"],
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
            SearchRequest search_req;
            SearchResult result;
            for (const auto& path : jbody["paths"]) {
                if(path.get<std::string>().find(prefix) != 0) {
                    res->result(http::status::bad_request);
                    spdlog::warn("Path {} is not under the prefix {}, url: {}", path.get<std::string>(), prefix, std::string(req->target()));      
                    co_return;
                }
                search_req.paths.push_back(path.get<std::string>());
            }
            for (const auto& query : jbody["querys"]) {
                QueryString qs;
                qs.query = query["query"].get<std::string>();
                qs.type = query["type"].get<std::string>();
                search_req.queries.emplace_back(std::move(qs));
            }
            search_req.start_time = jbody["start_time"].get<uint64_t>();
            search_req.end_time = jbody["end_time"].get<uint64_t>();
            if (jbody.contains("max_results")) {
                search_req.max_results = jbody["max_results"].get<std::size_t>();
            }
            if(search_req.paths.empty() || search_req.queries.empty()) {
                res->result(http::status::bad_request);
                spdlog::warn("Search request must contain at least one path and one query, url: {}", std::string(req->target()));      
                co_return;
            }

            LogSearcher searcher(indexer_);
            searcher.search(search_req,result);
            if (result.status != 0) {
                res->result(http::status::internal_server_error);
                spdlog::error("Search failed: {}, url: {}", result.error_msg, std::string(req->target()));
                co_return;
            }
            //output
            //{"status":0,"error_msg":"",
            //"records":[{"path":"","status":0,"error_msg":"","start_time":0,"end_time":0,"lines":[{"line":"","time":0},{...}]}]}
            nlohmann::json jres;
            jres["status"] = result.status;
            jres["error_msg"] = result.error_msg;
            jres["records"] = nlohmann::json::array();
            for (const auto& fm_ptr : result.matches) {
                if(fm_ptr->status != 0) {
                    continue;
                }
                if(fm_ptr->lines.empty()) {
                    continue;
                }
                nlohmann::json jfm;
                jfm["path"] = fm_ptr->path;
                jfm["status"] = fm_ptr->status;
                jfm["error_msg"] = fm_ptr->error_msg;
                jfm["lines"] = nlohmann::json::array();
                for (const auto& logline : fm_ptr->lines) {
                    nlohmann::json jline;
                    jline["line"] = logline.line;
                    jline["time"] = logline.timestamp;
                    jfm["lines"].push_back(jline);
                }
                jfm["start_time"] = fm_ptr->lines.front().timestamp;
                jfm["end_time"] = fm_ptr->lines.back().timestamp;
                jres["records"].push_back(jfm);
            }
            std::string res_body_j = jres.dump();
            //gzip compress if needed
            std::string res_body_c;
            std::string content_encoding;
            auto& headers = req->base();
            auto it = headers.find(boost::beast::http::field::accept_encoding);
            if (it != headers.end()) {
                auto accept_encoding = it->value();
                compress_body(res_body_j,accept_encoding,res_body_c,content_encoding);
            }
            if(content_encoding == "gzip") {
                res->set(http::field::content_encoding, "gzip");
                res->body() = std::move(res_body_c);
            }
            else {
                res->body() = std::move(res_body_j);
            }
            res->set(http::field::content_type, "application/json");
            res->prepare_payload();
            spdlog::info("Search completed with {} file matches under request : {}", result.matches.size(), req->target());
            co_return;
        } catch ( const nlohmann::json::exception& e) {
            // best-effort 500
            res->result(http::status::internal_server_error);
            spdlog::error("JSON error in search handler: {}", e.what());
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
    
    void AgentHandler::compress_body(const std::string& input,const std::string& accept_encoding, 
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
    
} // namespace drlog
