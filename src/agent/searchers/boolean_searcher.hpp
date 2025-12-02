#pragma once
#include <vector>
#include <string>
#include <memory>
#include "base_searcher.hpp"

namespace drlog {
    // boolean_node: structure for boolean search logic
    struct boolean_node {
        enum class Type { WORD, NOT, AND, OR } type;

        // Only used when type == WORD
        std::string word;

        // Used when type == NOT / AND / OR
        std::vector<std::shared_ptr<boolean_node>> children;

        boolean_node(Type t) : type(t) {}
        boolean_node(const std::string& w) : type(Type::WORD), word(w) {}
    };


    class boolean_searcher : public base_searcher {
    public:
        boolean_searcher();
        ~boolean_searcher();
        // Parse the pattern string and build the boolean_pattern structure
        bool build_pattern(const std::string& pattern);
        bool  search_line(const std::string& line,std::vector<matched_word> &matched, bool with_res = false);
    private:
        //print search pattern to log debug
        void print_search_pattern(const std::string &pattern, std::shared_ptr<boolean_node> node, int depth = 0);
        std::shared_ptr<boolean_node> pattern_;
    };
} // namespace drlog