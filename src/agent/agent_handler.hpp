#pragma once
#include "indexer.hpp"
#include <memory>
#include "libs/bst-http/request_handler.hpp"
// Include Boost.Beast and Boost.Asio so this header is self-contained
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/asio.hpp>
#include <boost/asio/awaitable.hpp>
#include "searcher.hpp"

namespace drlog {
    // define namespace aliases used in method signatures
    namespace beast = boost::beast;
    namespace http = beast::http;
    namespace net = boost::asio;
    using tcp = net::ip::tcp;

    class AgentHandler {
    public:
        AgentHandler(std::shared_ptr<FileIndexer> idx);
        ~AgentHandler();

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
    private:
        void compress_body(const std::string& input,const std::string& accept_encoding, 
            std::string& output,std::string& content_encoding);

         std::string ensure_utf8(const std::string& s);
    private:
        std::shared_ptr<FileIndexer> indexer_;
    };
} // namespace drlog
