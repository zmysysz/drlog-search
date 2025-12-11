#include "indexer.hpp"
#include "agent_handler.hpp"
#include "src/util/config.hpp"
#include <iostream>
#include <memory>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/daily_file_sink.h>
#include "libs/bst-http/bst_http.hpp"
#include <ifaddrs.h>
#include <arpa/inet.h>
#include "src/util/util.hpp"
#include "libs/bst-http/http_client.hpp"

#define AGENT_ANNOUNCE_INTERVAL_SECONDS 10

bool ip_matches_prefix(const std::string& ip, const std::vector<std::string>& prefixes) {
    for (const auto& prefix : prefixes) {
        if (ip.find(prefix) == 0) {  // Check if the IP starts with the prefix
            return true;
        }
    }
    return false;
}

std::vector<std::string> get_local_ips(const std::string& interface_names = "", const std::string& ip_prefixes = "", bool ipv4_only = true) {
    std::vector<std::string> ip_addresses;
    struct ifaddrs *ifap, *ifa;
    struct sockaddr_in *sa;
    struct sockaddr_in6 *sa6;
    char *addr;

    if(interface_names.empty() && ip_prefixes.empty()) {
        spdlog::warn("No interface names or IP prefixes provided, returning empty IP list");
        return ip_addresses;
    }

    // Get network interfaces info
    if (getifaddrs(&ifap) == -1) {
        std::cerr << "Error getting network interfaces\n";
        return ip_addresses;
    }

    // Split interface names and prefixes by '|'
    std::vector<std::string> interfaces = drlog::util::split(interface_names, '|');
    std::vector<std::string> prefixes = drlog::util::split(ip_prefixes, '|');

    // Traverse all network interfaces
    for (ifa = ifap; ifa != nullptr; ifa = ifa->ifa_next) {
        // If interface_names is provided, filter by interface name
        if (!interface_names.empty() && std::find(interfaces.begin(), interfaces.end(), ifa->ifa_name) == interfaces.end()) {
            continue;  // Skip interfaces that do not match
        }

        // Check for IPv4 address
        if (ifa->ifa_addr->sa_family == AF_INET && ipv4_only) {
            sa = (struct sockaddr_in *)ifa->ifa_addr;
            addr = inet_ntoa(sa->sin_addr);
            std::string ip(addr);

            // If IP prefix is provided, filter by prefix
            if (!ip_prefixes.empty() && !ip_matches_prefix(ip, prefixes)) {
                continue;  // Skip IPs that don't match any prefix
            }

            ip_addresses.push_back(ip);
        }
        // Check for IPv6 address
        else if (ifa->ifa_addr->sa_family == AF_INET6 && !ipv4_only) {
            sa6 = (struct sockaddr_in6 *)ifa->ifa_addr;
            char ipv6[INET6_ADDRSTRLEN];
            inet_ntop(AF_INET6, &sa6->sin6_addr, ipv6, sizeof(ipv6));
            std::string ip(ipv6);

            // If IP prefix is provided, filter by prefix
            if (!ip_prefixes.empty() && !ip_matches_prefix(ip, prefixes)) {
                continue;  // Skip IPs that don't match any prefix
            }

            ip_addresses.push_back(ip);
        }
    }

    freeifaddrs(ifap);
    return ip_addresses;
}

void run_announce_task(const std::vector<std::string>& registry_address, const std::string& registry_agent_address) {
    std::thread([registry_address, registry_agent_address]() {
        bst::http_client client;
        client.set_request_timeout(10);
        while (true) {
            for (const auto& address : registry_address) {
                
                bst::request req;
                req.url = "http://" + address + "/agent/announce?agent_addr=" + drlog::util::url_encode(registry_agent_address);
                bst::response res;
                try {
                    int status = client.get(req, res);
                    if (status == 200) {
                        spdlog::debug("Successfully announced to registry: {}", req.url);
                    } else {
                        spdlog::warn("Failed to announce to registry: {}. Status: {}", req.url, status);
                    }
                } catch (const std::exception& e) {
                    spdlog::error("Exception while announcing to registry: {}: {}", req.url, e.what());
                }
            }
            std::this_thread::sleep_for(std::chrono::seconds(AGENT_ANNOUNCE_INTERVAL_SECONDS));
        }
    }).detach();
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        spdlog::error("Usage: drlog-agent <config.json>");
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
    unsigned short port = 8113;
    unsigned threads = 1;
    unsigned scan_interval = 60;
    std::string log_path = "logs/";
    std::string log_level = "info";
    std::string cache_path = "cache/";
    std::vector<std::string> registry_address;
    std::string registry_agent_address;
    std::string interface_names;
    std::string ip_prefixes;

    if (cfg.contains("server")) {
        auto s = cfg["server"];
        if (s.contains("address")) address = s["address"].get<std::string>();
        if (s.contains("port")) port = static_cast<unsigned short>(s["port"].get<int>());
        if (s.contains("threads")) threads = s["threads"].get<unsigned>();
        if (s.contains("scan_interval")) scan_interval = s["scan_interval"].get<unsigned>();
        if (s.contains("logpath")) log_path = s["logpath"].get<std::string>();
        if (s.contains("loglevel")) log_level = s["loglevel"].get<std::string>();
        if (s.contains("cache_path")) cache_path = s["cache_path"].get<std::string>();
        if (s.contains("registry_address") && s["registry_address"].is_array()) {
            for (const auto& addr : s["registry_address"]) {
                registry_address.push_back(addr.get<std::string>());
            }
        }
        if (s.contains("registry_agent_address"))
            registry_agent_address = s["registry_agent_address"].get<std::string>();
        if (s.contains("agent_interface_names"))
            interface_names = s["agent_interface_names"].get<std::string>();
        if (s.contains("agent_ip_patterns"))
            ip_prefixes = s["agent_ip_patterns"].get<std::string>();
    }

    //for logger 
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
    
    // Get local IPs based on interface names and IP prefixes
    if(registry_agent_address.empty()) {
        auto local_ips = get_local_ips(interface_names, ip_prefixes, true);
        if(!local_ips.empty()) {
            registry_agent_address = local_ips[0] + ":" + std::to_string(port);
        }
    }
    if(registry_agent_address.empty()) {
        spdlog::error("No valid registry agent address could be determined.");
        return 1;
    }
    spdlog::info("Registry agent address: {}", registry_agent_address);

    //initialize the file indexer
    auto indexer = std::make_shared<drlog::FileIndexer>(scan_interval);
    indexer->set_index_interval_seconds(300);      // default 300s
    indexer->set_index_count_threshold(50000);     // default 50000 lines
    indexer->set_cache_path(cache_path);

    // Load multiple paths from config: expecting "paths": [ { "path": "...", "namepattern": "...", ... }, ... ]
    if (cfg.contains("paths") && cfg["paths"].is_array()) {
        for (const auto& p : cfg["paths"]) {
            if (!p.contains("path")) continue;
            std::string root = p["path"].get<std::string>();    // root path to scan, e.g. "/var/log/"
            std::string name_pattern = ".*";
            std::string time_format_pattern = "";
            //regex pattern to match the path, use check the request prefix, e.g. "^/var/log/.+/.+", prefix must be "/var/log/xxx/xxx"
            std::string path_pattern = ""; 
            std::string prefix_pattern = "";
            int max_days = 30;
            if (p.contains("maxdays")) max_days = p["maxdays"].get<int>();
            if (p.contains("prefixpattern")) prefix_pattern = p["prefixpattern"].get<std::string>();
            if (p.contains("namepattern")) name_pattern = p["namepattern"].get<std::string>();
            if (p.contains("time_format_pattern")) time_format_pattern = p["time_format_pattern"].get<std::string>();
            if (p.contains("pathpattern")) path_pattern = p["pathpattern"].get<std::string>();
            indexer->add_root(root, name_pattern, time_format_pattern, path_pattern, prefix_pattern, max_days);
            spdlog::info("Added root path: {} with name pattern: {}, path pattern: {}, prefix pattern: {}, max days: {}", 
                root, name_pattern, path_pattern, prefix_pattern, max_days);
            std::cout << "Added root path: " << root 
                << " with name pattern: " << name_pattern 
                << ", path pattern: " << path_pattern 
                << ", prefix pattern: " << prefix_pattern
                << ", max days: " << max_days << std::endl;
        }
    }
    indexer->init_indexes();
    indexer->start();

    // Initialize the HTTP server
    bst::http_server server;
    server.init(address, port, threads);
    server.set_max_request_body_size(100 * 1024 * 1024);
    // Register the request handlers
    std::shared_ptr<drlog::AgentHandler> handler = std::make_shared<drlog::AgentHandler>(indexer);
    bst::request_handler::register_route("/hello", std::bind(&drlog::AgentHandler::hello, handler, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
    bst::request_handler::register_route("/log/list", std::bind(&drlog::AgentHandler::list, handler, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3)); 
    bst::request_handler::register_route("/log/search", std::bind(&drlog::AgentHandler::search, handler, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
    
    run_announce_task(registry_address, registry_agent_address);

    spdlog::info("Server start on {}:{} with {} threads", address, port, threads);
    std::cout << "***************Server start****************" << std::endl;
    std::cout << "Address: " << address << std::endl;
    std::cout << "Port: " << port << std::endl;
    std::cout << "Threads: " << threads << std::endl;
    std::cout << "Registry Addresses: ";
    for (const auto& addr : registry_address) {
        std::cout << addr << " ";
    }
    std::cout << std::endl;
    std::cout << "******************************************" << std::endl;
    
    server.run_server();
    
    return 0;
}
