#pragma once
#include <vector>
#include <string>
#include <memory>

namespace drlog {
    enum SearchType {
        SIMPLE,
        BOOL,
        REGEX,
        ALL
    };

    struct matched_word {
        std::string word;
        size_t pos;
    };

    class base_searcher {
    public:
        base_searcher(SearchType type) : search_type_(type) {}
        virtual ~base_searcher() = default;
        virtual bool build_pattern(const std::string& pattern) = 0;
        virtual bool  search_line(const std::string& line,std::vector<matched_word> &matched, bool with_res = false) = 0;
        SearchType get_search_type() const {
            return search_type_;
        }
    private:
        SearchType search_type_; // 0:string, 1: bool, 2: regex
    };
} // namespace drlog