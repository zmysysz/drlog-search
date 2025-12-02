#include "agent_manager.hpp"
#include "src/util/util.hpp"
#include <spdlog/spdlog.h>
#include <thread>
#include <chrono>

namespace drlog {

    #define AGENT_TIMEOUT_SECONDS 60

    AgentManager::AgentManager(){}
    AgentManager::~AgentManager() {}

    void AgentManager::run_cleanup_task() {
        // run cleanup task every AGENT_TIMEOUT_SECONDS
        std::thread([this]() {
            while (true) {
                std::this_thread::sleep_for(std::chrono::seconds(AGENT_TIMEOUT_SECONDS));
                this->cleanup_expired_agents();
            }
        }).detach();
    }

    void AgentManager::add_agent(const std::string& agent_addr) {
        auto agent_id = std::to_string(util::MurMurHash64(agent_addr.data(), agent_addr.size(), 0));
        uint64_t now = static_cast<uint64_t>(std::time(nullptr));
        uint64_t last_announce = 0;
        {
            std::unique_lock<std::shared_mutex> lock(agents_mutex_);
            auto it = agents_.find(agent_id);
            if (it != agents_.end()) {
                // update last announce time
                last_announce = it->second->last_announce;
                it->second->last_announce = static_cast<uint64_t>(std::time(nullptr));
            } else {
                // new agent
                agents_[agent_id] = std::make_shared<agent_info>(agent_info{agent_addr, agent_id, static_cast<uint64_t>(std::time(nullptr))});
            }
        }
    }

    
    std::shared_ptr<agent_info> AgentManager::get_agent_by_id(const std::string& agent_id) {
        std::shared_lock<std::shared_mutex> lock(agents_mutex_);
        auto it = agents_.find(agent_id);
        if (it != agents_.end()) {
            // compare the last announce time
            auto now = static_cast<uint64_t>(std::time(nullptr));
            if (now - it->second->last_announce > AGENT_TIMEOUT_SECONDS) {
                return nullptr;
            }
            return it->second;
        }
        return nullptr;
    }

    std::vector<std::shared_ptr<agent_info>> AgentManager::get_active_agents() {
        std::vector<std::shared_ptr<agent_info>> active_agents;
        auto now = static_cast<uint64_t>(std::time(nullptr));
        {
            std::shared_lock<std::shared_mutex> lock(agents_mutex_);
            for (const auto& [id, agent] : agents_) {
                if (now - agent->last_announce <= AGENT_TIMEOUT_SECONDS) {
                    active_agents.push_back(agent);
                }
            }
        }
        return active_agents;
    }

    void AgentManager::cleanup_expired_agents() {
        auto now = static_cast<uint64_t>(std::time(nullptr));
        std::vector<std::string> expired_agents;
        int no_expired = 0; 
        {
            std::unique_lock<std::shared_mutex> lock(agents_mutex_);
            for (auto it = agents_.begin(); it != agents_.end(); ) {
                if (now - it->second->last_announce > AGENT_TIMEOUT_SECONDS) {
                    expired_agents.push_back(it->second->address);
                    it = agents_.erase(it);
                } else {
                    no_expired++;
                    ++it;
                }
            }
        }
        for (const auto& addr : expired_agents) {
            spdlog::info("Agent expired and removed: {}", addr);
        }
        spdlog::debug("Agent cleanup completed, {} expired agents removed, {} active agents remain", expired_agents.size(), no_expired);
    }
} // namespace drlog
