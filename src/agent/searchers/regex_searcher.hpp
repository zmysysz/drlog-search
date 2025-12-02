#pragma once
#include <boost/regex.hpp>
#include <memory>
#include <spdlog/spdlog.h>
#include "base_searcher.hpp"

namespace drlog {
    class regex_searcher : public base_searcher { 
        public:
            regex_searcher() : base_searcher(SearchType::REGEX) {}
            ~regex_searcher(){}
            // Parse the pattern string and build the boolean_pattern structure
            bool build_pattern(const std::string& pattern){
                try{
                    pattern_ = std::make_shared<boost::regex>(pattern);
                    return true;
                }catch(const boost::regex_error& e){
                    spdlog::error("Error building regex pattern: {}", e.what());
                }
                return false; 
            }
            bool search_line(const std::string& line,std::vector<matched_word> &matched, bool with_res = false){
                try{
                    boost::smatch results;
                    if (boost::regex_search(line, results, *pattern_)) {
                        if(with_res) {
                            matched.clear();
                            for (int i = 0; i < results.size(); ++i) {
                                matched.emplace_back(results.str(i),results.position(i));
                            }
                        }
                        return true;
                    }
                    return false;
                }
                catch(const boost::regex_error& e){
                    spdlog::error("Error searching regex pattern: {}", e.what());
                }
                return false;
            }
        private:
            std::shared_ptr<boost::regex> pattern_;
    };
} // namespace drlog