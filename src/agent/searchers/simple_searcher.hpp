#pragma once

#include <memory>
#include <spdlog/spdlog.h>
#include "base_searcher.hpp"

namespace drlog {
    class simple_searcher : public base_searcher { 
        public:
            simple_searcher() : base_searcher(SearchType::SIMPLE) {}
            ~simple_searcher(){}
            // Parse the pattern string and build the boolean_pattern structure
            bool build_pattern(const std::string& pattern){
                pattern_ = pattern;
                return true;
            }
            bool search_line(const std::string& line,std::vector<matched_word> &matched, bool with_res = false){
                size_t pos = line.find(pattern_);
                if (pos != std::string::npos) {
                    if(with_res) {
                        matched.clear();
                        matched.emplace_back(pattern_, pos);
                    }
                    return true;
                }
                return false;
            }
        private:
            std::string pattern_;
    };
} // namespace drlog