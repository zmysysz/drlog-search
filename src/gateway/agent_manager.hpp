#pragma once
#include <memory>
#include <string>
#include <map>
#include <vector>
#include <shared_mutex>

namespace drlog {

    struct agent_file_index {
        std::string path;
        uint64_t size;
        uint64_t mtime;
        std::string etag;
        uint64_t start_time;
        uint64_t end_time;
        std::string agent_id;
    };

    struct agent_file_indexes {
        std::string prefix;
        uint64_t last_updated;
        std::vector<agent_file_index> indexes;
    };

    struct agent_info {
        std::string address;
        std::string agent_id;
        uint64_t last_announce;
        std::map<std::string,std::shared_ptr<agent_file_indexes>> file_index_cache;    //prefix -> file index
    };

    class AgentManager {
    public:
        AgentManager();
        ~AgentManager();
        void add_agent(const std::string& agent_addr);
        std::shared_ptr<agent_info> get_agent_by_id(const std::string& agent_id);
        std::vector<std::shared_ptr<agent_info>> get_active_agents();
        void run_cleanup_task();
    private:
        void cleanup_expired_agents();
    private:
        std::map<std::string,std::shared_ptr<agent_info>> agents_;
        std::shared_mutex agents_mutex_;
    };
} // namespace drlog