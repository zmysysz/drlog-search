#include "gateway_handler.hpp"
#include "src/util/config.hpp"
#include <iostream>
#include <memory>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/daily_file_sink.h>
#include "libs/bst-http/bst_http.hpp"
#include "agent_manager.hpp"

int main(int argc, char* argv[]) {
    if (argc < 2) {
        spdlog::error("Usage: drlog-gateway <config.json>");
        return 1;
    }
    std::string config_path = argv[1];
    auto cfg = load_config(config_path);
    if (cfg.is_null()) {
        spdlog::error("Invalid or empty config: {}", config_path);
        return 1;
    }

    // Server defaults
    std::string address = "0.0.0.0";
    unsigned short port = 8111;
    unsigned threads = 1;
    unsigned scan_interval = 60;
    std::string log_path = "logs/";
    std::string log_level = "info";
    std::string cache_path = "cache/";
    std::string web_path = "web/";

    if (cfg.contains("server")) {
        auto s = cfg["server"];
        if (s.contains("address")) address = s["address"].get<std::string>();
        if (s.contains("port")) port = static_cast<unsigned short>(s["port"].get<int>());
        if (s.contains("threads")) threads = s["threads"].get<unsigned>();
        if (s.contains("scan_interval")) scan_interval = s["scan_interval"].get<unsigned>();
        if (s.contains("logpath")) log_path = s["logpath"].get<std::string>();
        if (s.contains("loglevel")) log_level = s["loglevel"].get<std::string>();
        if (s.contains("cache_path")) cache_path = s["cache_path"].get<std::string>();
        if (s.contains("webpath")) web_path = s["webpath"].get<std::string>();
    }

    auto logger = spdlog::daily_logger_mt("daily_logger",log_path.append("/server.log"), 0, 0, false, 7);
    spdlog::set_default_logger(logger);
    spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] %v");
    
    if (log_level == "debug") {
        spdlog::set_level(spdlog::level::debug);
    } else if (log_level == "warn") {
        spdlog::set_level(spdlog::level::warn);
    } else if (log_level == "error") {
        spdlog::set_level(spdlog::level::err);
    } else if (log_level == "trace") {
        spdlog::set_level(spdlog::level::trace);
    } else {
        spdlog::set_level(spdlog::level::info);
    }
    logger->flush_on(spdlog::level::info);

    std::shared_ptr<drlog::AgentManager> agent_manager = std::make_shared<drlog::AgentManager>();
    agent_manager->run_cleanup_task();
  
    // Initialize the HTTP server
    bst::http_server server;
    server.init(address, port, threads);
    server.set_max_request_body_size(100 * 1024 * 1024);
    // Register the request handlers
    std::shared_ptr<drlog::GTHandler> handler = std::make_shared<drlog::GTHandler>(agent_manager);
    handler->set_web_path(web_path);
    bst::request_handler::register_route("/hello", std::bind(&drlog::GTHandler::hello, handler, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
    bst::request_handler::register_route("/log/list", std::bind(&drlog::GTHandler::list, handler, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3)); 
    bst::request_handler::register_route("/log/search", std::bind(&drlog::GTHandler::search, handler, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
    bst::request_handler::register_route("/agent/announce", std::bind(&drlog::GTHandler::announce, handler, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
    bst::request_handler::register_route("/agent/list", std::bind(&drlog::GTHandler::agent_list, handler, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
    bst::request_handler::register_route("/web", std::bind(&drlog::GTHandler::web, handler, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));

    spdlog::info("Server start on {}:{} with {} threads", address, port, threads);
    std::cout << "***************Server start****************" << std::endl;
    std::cout << "Address: " << address << std::endl;
    std::cout << "Port: " << port << std::endl;
    std::cout << "Threads: " << threads << std::endl;
    std::cout << "******************************************" << std::endl;
    
    server.run_server();
    
    return 0;
}
