#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <memory>
#include "indexer.hpp"
#include "searchers/base_searcher.hpp"

namespace drlog {

    struct LogLine {
        uint64_t timestamp;
        std::string line;
    };

    struct FileMatches {
        std::string path;
        std::vector<LogLine> lines;
        int status{0}; // 0=ok, other=error
        std::string error_msg;
    };

    struct QueryString {
        std::string query;
        std::string type; //simple, boolean, regex
    };

    struct SearchRequest {
        std::vector<QueryString> queries;
        uint64_t start_time{0};
        uint64_t end_time{0};
        std::vector<std::string> paths;
        std::string sort_type{""};  //
        std::size_t max_results{500};
    };

    struct SearchResult {
        std::vector<std::shared_ptr<FileMatches>> matches;
        int status{0}; // 0=ok, other=error
        std::string error_msg;
    };

    struct QuerySearcher {
        QueryString query_string;
        SearchType type;
        std::shared_ptr<base_searcher> searcher = nullptr;
    };

    struct SearchContext {
        std::string path;
        std::shared_ptr<SearchRequest> req;
        uint64_t start_time;
        uint64_t end_time;
        uint64_t index_start_time;
        uint64_t index_end_time;
        uint64_t index_start_pos;
        uint64_t index_end_pos;
        LogLine tmp_line;
        std::vector<LogLine> log_lines;
        std::vector<LogLine> matched_lines;
        std::shared_ptr<FileInfo> index_file_info;
        std::shared_ptr<SearchContext> sub;
        std::shared_ptr<SearchContext> parent;
        std::vector<QuerySearcher> searchers;
        int status{0}; // 0=ok, other=error
        std::string error_msg;
    };

    class LogSearcher {
    public:
        explicit LogSearcher(const std::shared_ptr<FileIndexer> indexer) : indexer_(indexer) {};
        ~LogSearcher() {};
        void search(const SearchRequest& req, SearchResult& result);
    private:
        bool build_searchers(std::shared_ptr<SearchContext> ctx);
        bool find_index_pos(std::shared_ptr<SearchContext> ctx);
        void search_file(std::shared_ptr<SearchContext> ctx);
        void search_file_txt(std::shared_ptr<SearchContext> ctx);
        void search_file_gzip(std::shared_ptr<SearchContext> ctx);
        void search_file_igzip(std::shared_ptr<SearchContext> ctx);
        void get_lines_gzip(std::string &carry,std::vector<std::string> &lines,size_t pos);
        bool timestamp_covers(uint64_t idx_satrt, uint64_t idx_end, uint64_t start_time, uint64_t end_time);
        bool parse_line(std::shared_ptr<SearchContext> ctx, std::string &line);
        bool exec_searchers(std::shared_ptr<SearchContext> ctx);
    private:
        std::shared_ptr<FileIndexer> indexer_;
    };
} // namespace drlog