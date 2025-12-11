#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <shared_mutex>
#include <regex>
#include <thread>
#include <atomic>
#include <cstdint>
#include <memory>

namespace drlog {

    struct time_format {
        std::string format;  
        std::regex regex_pattern;
    };
    
    struct RootPath {
        std::string path;
        std::string path_pattern;
        std::string filename_pattern;
        std::string time_format_pattern;
        std::regex path_regex;
        std::regex filename_regex;
        std::regex time_format_regex;
        int max_days{30};
    };

    struct TimeIndex {
        uint64_t timestamp;
        uint64_t offset;
    };

    struct FileIndex {
        std::string index_etag;
        std::time_t last_index_time;
        std::vector<TimeIndex> time_indexes;
    };

    struct FileInfo {
        std::string name;
        std::string dir;
        std::string fullpath;
        std::uint64_t size{0};
        std::time_t mtime{0};
        std::string file_type;
        // weak ETag generated from size + mtime (hex), and optional md5 (empty unless computed)
        std::string etag;
        uint64_t inode{0};
        std::shared_ptr<FileIndex> file_index;
        RootPath root_path;
    };

    class FileIndexer {
    public:
        explicit FileIndexer(unsigned scan_interval_secs);
        ~FileIndexer();

        // add a root path and filename regex
        void add_root(const std::string& root_path, const std::string& filename_pattern, 
            const std::string& time_format_pattern, const std::string& path_pattern);
        void init_indexes();
        // background scanner control
        void start();
        void stop();

        // query by prefix (prefix matches beginning of fullpath)
        std::vector<FileInfo> list_prefix(const std::string& prefix) const;

        // set indexing interval in seconds (e.g. 60, 300)
        void set_index_interval_seconds(unsigned seconds) { index_interval_seconds_ = seconds; }
        // set count threshold to force index creation after N lines
        void set_index_count_threshold(std::size_t count) { index_count_threshold_ = count; }
        void set_scan_interval_seconds(unsigned seconds) { scan_interval_seconds_ = seconds; }  
        void set_cache_path(const std::string& path) { cache_path_ = path; }
        bool get_file_index_by_path(const std::string& path, FileInfo& out_info) const;
        std::time_t get_timestamp_from_log_line(const std::string &line);
        std::time_t get_timestamp_from_log_line(const std::string_view &line);

    private:
        void scan_loop();
        void scan_root(RootPath const& rp);
        void update_file_index();
        void update_file_index_txt(const std::string& path, FileInfo& file_info);
        void update_file_index_txt_mmap(const std::string& path, FileInfo& file_info);
        void update_file_index_gzip(const std::string& path, FileInfo& file_info);
        void update_file_index_igzip(const std::string& path, FileInfo& file_info);
        void save_index_to_cache();
        void load_index_from_cache();
        void remove_unused_indexes();

    private:
        mutable std::shared_mutex mutex_;
        std::unordered_map<std::string, FileInfo> index_; // key = fullpath

        std::vector<RootPath> roots_;
        mutable std::shared_mutex roots_mutex_;

        std::thread worker_;
        std::atomic<bool> running_;
        unsigned scan_interval_seconds_;
        std::vector<time_format> time_formats_;
        // indexing policy: time interval and count threshold
        unsigned index_interval_seconds_;      // default interval
        std::size_t index_count_threshold_;  // default count threshold
        std::string cache_path_{"cache/"};
        int updated_index_count_{0};
    };
}   // namespace drlog