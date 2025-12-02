#pragma once
#include <memory>
#include "libs/bst-http/request_handler.hpp"
// Include Boost.Beast and Boost.Asio so this header is self-contained
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/asio.hpp>
#include <boost/asio/awaitable.hpp>
#include "agent_manager.hpp"
#include <nlohmann/json.hpp>

namespace drlog {
    // define namespace aliases used in method signatures
    namespace beast = boost::beast;
    namespace http = beast::http;
    namespace net = boost::asio;
    using tcp = net::ip::tcp;

    class GTHandler {
    public:
        GTHandler(std::shared_ptr<AgentManager> agent_manager);
        ~GTHandler();

        // Handle the request
        net::awaitable<void> hello(std::shared_ptr<http::request<http::string_body>> req,
            std::shared_ptr<http::response<http::string_body>> res,
            std::shared_ptr<bst::request_context> ctx);
        
        net::awaitable<void> list(std::shared_ptr<http::request<http::string_body>> req,
            std::shared_ptr<http::response<http::string_body>> res,
            std::shared_ptr<bst::request_context> ctx);
        
        net::awaitable<void> search(std::shared_ptr<http::request<http::string_body>> req,
            std::shared_ptr<http::response<http::string_body>> res,
            std::shared_ptr<bst::request_context> ctx);

        net::awaitable<void> announce(std::shared_ptr<http::request<http::string_body>> req,
            std::shared_ptr<http::response<http::string_body>> res,
            std::shared_ptr<bst::request_context> ctx);

        net::awaitable<void> agent_list(std::shared_ptr<http::request<http::string_body>> req,
            std::shared_ptr<http::response<http::string_body>> res,
            std::shared_ptr<bst::request_context> ctx);
        
        net::awaitable<void> web(std::shared_ptr<http::request<http::string_body>> req,
            std::shared_ptr<http::response<http::string_body>> res,
            std::shared_ptr<bst::request_context> ctx);

        void set_web_path(const std::string& web_path) {web_path_ = web_path;}

    private:
        void compress_body(const std::string& input,const std::string& accept_encoding, 
            std::string& output,std::string& content_encoding);
        void decompress_body(const std::string& input,const std::string& content_encoding, 
            std::string& output);
        net::awaitable<void> get_agent_log_lists(const std::string &prefix,std::vector<std::shared_ptr<agent_info>> agents,
            std::vector<agent_file_index>& out_indexes, std::shared_ptr<bst::request_context> ctx);
        net::awaitable<void> get_agent_search(const std::string &prefix,
            const nlohmann::json &jreq_body,std::shared_ptr<bst::request_context> ctx);
    private:
        std::shared_ptr<AgentManager> agent_manager_;
        std::string web_path_;
    };
} // namespace drlog
