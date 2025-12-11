#include "indexer.hpp"
#include <filesystem>
#include <iostream>
#include <mutex>
#include <chrono>
#include <type_traits>
#include <sstream>
#include <iomanip>
#include <spdlog/spdlog.h>
#include <fstream>
#include <map>
#include <algorithm>
#include <zlib.h>
#include <cstring>
#include <ctime>
#include <sys/stat.h>
#include <unistd.h>
#include <nlohmann/json.hpp>
#include <cstdio>
#include "src/util/util.hpp"
#include <sys/mman.h>
#include <fcntl.h>
#include "libs/date.h"
#include "libs/igzip/igzip_lib.h"

namespace drlog {
    namespace fs = std::filesystem;
    using json = nlohmann::json;

    FileIndexer::FileIndexer(unsigned scan_interval_secs)
        : running_(false), scan_interval_seconds_(scan_interval_secs) {
            time_formats_ = {
                {"%Y-%m-%d %H:%M:%S", std::regex(R"(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2})")},
                {"%Y/%m/%d %H:%M:%S", std::regex(R"(\d{4}/\d{2}/\d{2} \d{2}:\d{2}:\d{2})")},
                {"%d/%b/%Y:%H:%M:%S", std::regex(R"(\d{2}/[A-Za-z]{3}/\d{4}:\d{2}:\d{2}:\d{2})")},
                {"%b %d %H:%M:%S", std::regex(R"([A-Za-z]{3} \d{2} \d{2}:\d{2}:\d{2})")},
                {"%Y-%m-%dT%H:%M:%S", std::regex(R"(\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2})")},
                {"%Y-%m-%dT%H:%M:%S%z", std::regex(R"(\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}[+\-]\d{4})")},
                {"%a, %d %b %Y %H:%M:%S %Z", std::regex(R"([A-Za-z]{3}, \d{2} [A-Za-z]{3} \d{4} \d{2}:\d{2}:\d{2} [A-Za-z]{3})")}
            };
            // ensure indexing policy has sane defaults to avoid uninitialized use
            index_interval_seconds_ = 300;
            index_count_threshold_ = 50000;
        }

    FileIndexer::~FileIndexer() {
        stop();
    }

    // replace add_root implementation to accept time_format_regex
    void FileIndexer::add_root(const std::string& root_path, const std::string& filename_pattern, 
        const std::string& time_format_pattern, const std::string& path_pattern, const std::string& prefix_pattern, int max_days) {
        try {
             RootPath rp;
             rp.path = root_path;
             if (!filename_pattern.empty()) {
                rp.filename_pattern = filename_pattern;
                try { rp.filename_regex = std::regex(filename_pattern); }
                catch (const std::exception& e) {
                    spdlog::warn("Bad filename pattern '{}': {}", filename_pattern, e.what());
                }
            }
            if (!time_format_pattern.empty()) {
                rp.time_format_pattern = time_format_pattern;
                try { rp.time_format_regex = std::regex(time_format_pattern); }
                catch (const std::exception& e) {
                    spdlog::warn("Bad time format pattern '{}': {}", time_format_pattern, e.what());
                }
            }
            if (!path_pattern.empty()) {
                rp.path_pattern = path_pattern;
                try { rp.path_regex = std::regex(path_pattern); }
                catch (const std::exception& e) {
                    spdlog::warn("Bad path pattern '{}': {}", path_pattern, e.what());
                }
            }
            if (!prefix_pattern.empty()) {
                rp.prefix_pattern = prefix_pattern;
                try { rp.prefix_regex = std::regex(prefix_pattern); }
                catch (const std::exception& e) {
                    spdlog::warn("Bad prefix pattern '{}': {}", prefix_pattern, e.what());
                }
            }
            rp.max_days = max_days;
            {
                std::unique_lock<std::shared_mutex> lock(roots_mutex_);
                roots_.emplace_back(std::move(rp));
            }
        } catch (const std::exception& e) {
            spdlog::error("Bad root path '{}': {}", root_path, e.what());
        }
    }

    void FileIndexer::init_indexes() {
        //step1 scan the roots
        for (const auto& rp : roots_) scan_root(rp);
        //step2 load existing index from cache on startup
        load_index_from_cache();
        //step3 update file index
        update_file_index();
        //step4 remove unused indexes
        remove_unused_indexes();
        //step5 write to cache
        save_index_to_cache();
    }

    void FileIndexer::start() {
        bool expected = false;
        if (!running_.compare_exchange_strong(expected, true)) return;
        worker_ = std::thread([this]{ scan_loop(); });
    }

    void FileIndexer::stop() {
        bool expected = true;
        if (!running_.compare_exchange_strong(expected, false)) return;
        if (worker_.joinable()) worker_.join();
    }

    void FileIndexer::scan_loop() {
        while (running_) {
            try {
                std::vector<RootPath> roots_copy;
                {
                    std::shared_lock<std::shared_mutex> rlock(roots_mutex_);
                    roots_copy = roots_;
                }
                //step1 scan the roots
                for (const auto& rp : roots_copy) scan_root(rp);
                //step2 update file index
                update_file_index();
                //step3 remove unused indexes
                remove_unused_indexes();
                //step4 write to cache
                save_index_to_cache();
            } catch (const std::exception& e) {
                spdlog::error("Indexer scan error: {}", e.what());
            }
            for (unsigned i = 0; i < scan_interval_seconds_ && running_; ++i)
                std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }

    // Updated signature: accept RootPath const&
    void FileIndexer::scan_root(RootPath const& rp) {
        try {
            const std::string& root = rp.path;

            if (!fs::exists(root) || !fs::is_directory(root)) return;
            for (auto it = fs::recursive_directory_iterator(root, fs::directory_options::skip_permission_denied);
                it != fs::recursive_directory_iterator(); ++it)
            {
                try {
                    const auto& p = it->path();
                    // skip symbolic links
                    if (fs::is_symlink(p)) continue;

                    // get inode via POSIX stat
                    struct stat st {};
                    // use string() to obtain a stable char* for stat
                    if (stat(p.string().c_str(), &st) == 0) {
                        // set inode if available
                        // FileInfo::inode is uint64_t
                        // st.st_ino is implementation-defined width; cast to uint64_t
                    } else {
                        // if stat fails, skip this entry
                        continue;
                    }
                    if (!fs::is_regular_file(p)) continue;
                    
                    std::string filename = p.filename().string();
                    //match path and name pattern
                    try {
                        if (!rp.path_pattern.empty() && !std::regex_match(p.string(), rp.path_regex)) continue;
                        if (!rp.filename_pattern.empty() && !std::regex_match(filename, rp.filename_regex)) continue;
                    } catch (const std::regex_error& e) {
                        spdlog::warn("Regex error for path {}: {}", rp.path, e.what());
                        continue;
                    }
                    
                    FileInfo info;
                    info.name = filename;
                    info.dir = p.parent_path().string();
                    info.fullpath = p.string();
                    info.size = static_cast<std::uint64_t>(fs::file_size(p));
                    // set inode from stat result
                    info.inode = static_cast<uint64_t>(st.st_ino);

                    // portable conversion from filesystem::file_time_type to system_clock::time_point
                    auto ftime = fs::last_write_time(p);
                    using file_time_type = decltype(ftime);
                    using file_clock = file_time_type::clock;
                    std::chrono::system_clock::time_point sctp;
                    if constexpr (std::is_same_v<file_clock, std::chrono::system_clock>) {
                        sctp = std::chrono::system_clock::time_point(std::chrono::duration_cast<std::chrono::system_clock::duration>(ftime.time_since_epoch()));
                    } else {
                        auto now_file = file_clock::now();
                        auto now_sys = std::chrono::system_clock::now();
                        auto diff = ftime - now_file; // file_clock::duration
                        auto diff_sys = std::chrono::duration_cast<std::chrono::system_clock::duration>(diff);
                        sctp = now_sys + diff_sys;
                    }
                    info.mtime = std::chrono::system_clock::to_time_t(sctp);
                    // determine file type based on suffix
                    std::string lower = filename;
                    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c){ return std::tolower(c); });
                    if (lower.size() >= 3 && lower.substr(lower.size()-3) == ".gz") {
                        info.file_type = "gzip";
                    } else {
                        info.file_type = "text";
                    }
                    // compute cheap etag using util helper (size + mtime)
                    info.etag = util::etag_from_size_mtime(info.size, info.mtime);
                    info.root_path = rp;

                    // insert or update under unique lock
                    {
                        std::unique_lock<std::shared_mutex> lock(mutex_);
                        auto itmap = index_.find(info.fullpath);
                        if (itmap == index_.end() || itmap->second.inode != info.inode) {
                            index_.emplace(info.fullpath, info);
                            spdlog::info("Indexed new file: {} inode={}", info.fullpath, info.inode);
                        } else {
                            if (itmap->second.size != info.size || itmap->second.mtime != info.mtime) {
                                info.file_index = itmap->second.file_index; // preserve existing file_index
                                itmap->second = info; // update
                                spdlog::info("Updated file info: {} inode={}", info.fullpath, info.inode);
                            } else {
                                // no change
                                spdlog::debug("No change for file: {} inode={}", info.fullpath, info.inode);
                            }
                        }
                    }
                } catch (const std::exception& e) {
                    // skip individual file errors
                    spdlog::error("scan_root entry error: {}", e.what());
                }
            }
        } catch (const std::exception& e) {
            spdlog::error("scan_root error for {}: {}", rp.path, e.what());
        }
    }

    void FileIndexer::update_file_index() {
        updated_index_count_ = 0;
        std::vector<std::pair<std::string, FileInfo>> snapshot;
        {
            std::shared_lock<std::shared_mutex> lock(mutex_);
            snapshot.reserve(index_.size());
            for (const auto& kv : index_) {
                snapshot.emplace_back(kv.first, kv.second);
                snapshot.back().second.file_index = std::make_shared<FileIndex>();
                if(kv.second.file_index) {
                    // copy existing index data
                    *(snapshot.back().second.file_index) = *(kv.second.file_index);
                }
            }
        }

        for (auto& kv : snapshot) {
            const std::string& path = kv.first;
            FileInfo &info = kv.second;
            bool need_update = false;
            if (!info.file_index) {
                need_update = true;
            } else if (info.file_index->index_etag != info.etag) {
                need_update = true;
            }
            if (!need_update) continue;

            try {
                // choose handler based on suffix
                if (info.file_type == "gzip") {
                    update_file_index_igzip(path, info);
                } else {
                    update_file_index_txt_mmap(path, info);
                }
                // update index_ with new file_index, index_etag, last_index_time
                {
                    std::unique_lock<std::shared_mutex> wlock(mutex_);
                    auto it = index_.find(path);
                    if (it != index_.end()) {
                        // apply changes
                        it->second.file_index = info.file_index;
                        it->second.file_index->index_etag = info.etag;
                        it->second.file_index->last_index_time = std::time(nullptr);
                    }
                }
                updated_index_count_++;
            } catch (const std::exception& e) {
                spdlog::error("Failed to update index for {}: {}", path, e.what());
            }
        }
    }

    // update file index for plain text file
    void FileIndexer::update_file_index_txt(const std::string& path, FileInfo& file_info) {
        // parse text file and build time index
        double d1 = util::get_micro_timestamp();
        std::ifstream ifs(path, std::ios::binary);
        if (!ifs.is_open()) {
            spdlog::warn("Failed to open text file for indexing: {}", path);
            return;
        }
        //get file size
        ifs.seekg(0, std::ios::end);
        uint64_t file_size = (uint64_t)ifs.tellg();
        ifs.seekg(0, std::ios::beg);

        std::vector<TimeIndex> entries;
        entries.reserve(1024);
        std::string line;
        std::time_t last_recorded_bucket = 0;
        std::size_t lines_since_last = 0;
        const unsigned interval = index_interval_seconds_;
        const std::size_t count_threshold = index_count_threshold_;
        std::size_t skipped_lines = 0;

        if (file_info.file_index && !file_info.file_index->time_indexes.empty()) {
            // start from last indexed offset
            auto &index_entries = file_info.file_index->time_indexes;
            const TimeIndex& last_index = file_info.file_index->time_indexes.back();
            if (last_index.offset < file_size) {
                ifs.seekg(static_cast<std::streampos>(last_index.offset));
                // insert existing index entries into entries vector
                entries.insert(entries.end(), index_entries.begin(), index_entries.end() - 1);
                last_recorded_bucket = static_cast<std::time_t>(last_index.timestamp);
            }
        }
        std::streampos last_start_pos;
        std::string last_line;
        while (ifs) {
            std::streampos start_pos = ifs.tellg();
            if (!std::getline(ifs, line))
            {
                // add the last index entry
                if (!entries.empty()) {
                    uint64_t offset = 0;
                    if (last_start_pos!= std::streampos(-1)) offset = static_cast<uint64_t>(last_start_pos);
                    std::time_t bucket = get_timestamp_from_log_line(last_line);
                    if(bucket!=0) {
                        entries.push_back(TimeIndex{static_cast<uint64_t>(bucket), offset});
                        std::string time_str = util::format_timestamp(bucket);
                        spdlog::debug("Last index entry for {}: bucket={} offset={} time={}", path, bucket, offset, time_str);
                    }
                }
                break;
            }
            std::time_t ts = get_timestamp_from_log_line(line);
            if (ts == 0) {
                // no timestamp -> skip and count
                ++skipped_lines;
                continue;
            }

            // bucket by interval
            std::time_t bucket = ts - (ts % interval);
            last_start_pos = start_pos;
            last_line = line;

            if (last_recorded_bucket == 0) {
                // first index entry
                uint64_t offset = 0;
                if (start_pos != std::streampos(-1)) offset = static_cast<uint64_t>(start_pos);
                entries.push_back(TimeIndex{static_cast<uint64_t>(bucket), offset});
                last_recorded_bucket = bucket;
                lines_since_last = 0;
                std::string time_str = util::format_timestamp(bucket);
                spdlog::debug("First index entry for {}: bucket={} offset={} time={}", path, bucket, offset,time_str);
                continue;
            }

            lines_since_last++;

            if (bucket >= static_cast<std::time_t>(last_recorded_bucket + interval) ||
                lines_since_last >= count_threshold) {
                uint64_t offset = 0;
                if (start_pos != std::streampos(-1)) offset = static_cast<uint64_t>(start_pos);
                entries.push_back(TimeIndex{static_cast<uint64_t>(bucket), offset});
                last_recorded_bucket = bucket;
                lines_since_last = 0;
                // debug: record created
                std::string time_str = util::format_timestamp(bucket);
                spdlog::debug("Added index entry for {}: bucket={} offset={} time={}", path, bucket, offset,time_str);
            }
        }

        // apply entries to file_info
        if (!file_info.file_index) file_info.file_index = std::make_shared<FileIndex>();
        file_info.file_index->time_indexes = std::move(entries);
        double d2 = util::get_micro_timestamp();
        spdlog::info("Indexed text file {} entries={} skipped_lines={} time_cost={}", path, file_info.file_index->time_indexes.size(), skipped_lines,(d2-d1)/1000.0);
    }

    void FileIndexer::update_file_index_txt_mmap(const std::string& path, FileInfo& file_info) {
        // Open the file using memory-mapped I/O
        double d1 = util::get_micro_timestamp();
        std::ifstream ifs(path, std::ios::binary);
        if (!ifs.is_open()) {
            spdlog::warn("Failed to open text file for indexing: {}", path);
            return;
        }

        ifs.seekg(0, std::ios::end);
        uint64_t file_size = static_cast<uint64_t>(ifs.tellg());
        ifs.seekg(0, std::ios::beg);
        ifs.close();

        std::vector<TimeIndex> entries;
        entries.reserve(1024);

        std::time_t last_recorded_bucket = 0;
        std::size_t lines_since_last = 0;
        const unsigned interval = index_interval_seconds_;
        const std::size_t count_threshold = index_count_threshold_;
        std::size_t skipped_lines = 0;

        // Memory-map the file
        int fd = open(path.c_str(), O_RDONLY);
        if (fd == -1) {
            spdlog::warn("Failed to open file descriptor for mmap: {}", path);
            return;
        }

        void* mapped = mmap(nullptr, file_size, PROT_READ, MAP_PRIVATE, fd, 0);
        if (mapped == MAP_FAILED) {
            spdlog::warn("Failed to mmap file: {}", path);
            close(fd);
            return;
        }

        const char* data = static_cast<const char*>(mapped);
        const char* end = data + file_size;
        const char* line_start = data;
        //check for existing index to resume from last offset
        if (file_info.file_index && !file_info.file_index->time_indexes.empty()) {
            // start from last indexed offset
            auto &index_entries = file_info.file_index->time_indexes;
            const TimeIndex& last_index = file_info.file_index->time_indexes.back();
            if (last_index.offset < file_size) {
                line_start = data + static_cast<std::ptrdiff_t>(last_index.offset);
                // insert existing index entries into entries vector
                entries.insert(entries.end(), index_entries.begin(), index_entries.end() - 1);
                last_recorded_bucket = static_cast<std::time_t>(last_index.timestamp);
            }
        }
        std::string_view last_line;
        uint64_t last_offset = 0;

        while (line_start < end) {
            const char* line_end = static_cast<const char*>(memchr(line_start, '\n', end - line_start));
            if (!line_end) {
                line_end = end; // Handle the last line without a newline
                break;
            }
            uint64_t offset = static_cast<uint64_t>(line_start - data);
            std::string_view line(line_start, line_end - line_start);
            last_line = line;
            last_offset = offset;
            std::time_t ts = get_timestamp_from_log_line(line);
            if (ts == 0) {
                ++skipped_lines;
                line_start = line_end + 1;
                continue;
            }

            std::time_t bucket = ts - (ts % interval);
            if (last_recorded_bucket == 0 ||
                bucket >= static_cast<std::time_t>(last_recorded_bucket + interval) ||
                lines_since_last >= count_threshold) {
                entries.push_back(TimeIndex{static_cast<uint64_t>(bucket), offset});
                last_recorded_bucket = bucket;
                lines_since_last = 0;
                std::string time_str = util::format_timestamp(bucket);
                spdlog::debug("Added index entry for {}: bucket={} offset={} time={}", path, bucket, offset, time_str);
            }

            ++lines_since_last;
            line_start = line_end + 1;
        }
        // add the last index entry
        if (!entries.empty() && !last_line.empty()) {
            uint64_t offset = 0;
            if (line_start != data) offset = last_offset;
            std::time_t bucket = get_timestamp_from_log_line(last_line);
            if(bucket!=0) {
                entries.push_back(TimeIndex{static_cast<uint64_t>(bucket), offset});
                std::string time_str = util::format_timestamp(bucket);
                spdlog::debug("Last index entry for {}: bucket={} offset={} time={}", path, bucket, offset, time_str);
            }
        }

        munmap(mapped, file_size);
        close(fd);

        // Apply entries to file_info
        if (!file_info.file_index) file_info.file_index = std::make_shared<FileIndex>();
        file_info.file_index->time_indexes = std::move(entries);
        double d2 = util::get_micro_timestamp();
        spdlog::info("Indexed text file {} entries={} skipped_lines={} time_cost={}", path, file_info.file_index->time_indexes.size(), skipped_lines,(d2-d1)/1000.0);
    }

    // update file index for gzip file (use zlib gzread) - robust offset handling
    void FileIndexer::update_file_index_gzip(const std::string& path, FileInfo& file_info) {
        // parse gzip file and build time index
        double d1 = util::get_micro_timestamp();
        gzFile gz = gzopen(path.c_str(), "rb");
        if (!gz) {
            spdlog::warn("Failed to open gzip file for indexing: {}", path);
            return;
        }

        const int BUF_SIZE = 16*1024;
        std::vector<char> buf(BUF_SIZE);
        std::string carry; // leftover partial line from previous chunk
        std::vector<TimeIndex> entries;
        entries.reserve(1024);

        uint64_t total_uncompressed = 0; // total uncompressed bytes BEFORE current chunk
        std::time_t last_recorded_bucket = 0;
        std::size_t lines_since_last = 0;
        const unsigned interval = index_interval_seconds_;
        const std::size_t count_threshold = index_count_threshold_;
        std::size_t skipped_lines = 0;
        
        uint64_t last_offset = 0;
        std::time_t last_bucket;
        std::string last_line;

        while (true) {
            int n = gzread(gz, buf.data(), BUF_SIZE);
            if (n < 0) {
                int errnum = 0;
                const char* errstr = gzerror(gz, &errnum);
                spdlog::warn("gzread error on {}: {}", path, errstr ? errstr : "unknown");
                break;
            }
            if (n == 0) {
                // end of file,add the last index entry
                if (!entries.empty()) {
                    uint64_t offset = 0;
                    if (last_offset!= 0) offset = last_offset;
                    last_bucket = get_timestamp_from_log_line(last_line);
                    entries.push_back(TimeIndex{static_cast<uint64_t>(last_bucket), offset});
                    std::string time_str = util::format_timestamp(last_bucket);
                    spdlog::debug("Last index entry for {}: bucket={} offset={} time={}", path, last_bucket, offset, time_str);
                }   
                break;
            }

            // Append exactly n bytes
            carry.append(buf.data(), static_cast<size_t>(n));

            // compute previous leftover size and base offset for this carry buffer
            size_t previous_carry_size = (carry.size() >= static_cast<size_t>(n)) ? (carry.size() - static_cast<size_t>(n)) : 0;
            uint64_t base_offset = (total_uncompressed >= previous_carry_size) ? (total_uncompressed - previous_carry_size) : 0;

            // scan lines in carry
            size_t pos = 0;
            size_t processed_up_to = 0;
            while (true) {
                size_t nl = carry.find('\n', pos);
                if (nl == std::string::npos) break;
                std::string line = carry.substr(pos, nl - pos);
                uint64_t line_start_offset = base_offset + pos;

                std::time_t ts = get_timestamp_from_log_line(line);
                if (ts == 0) {
                    // skip lines without timestamp and count them
                    ++skipped_lines;
                } else {
                    std::time_t bucket = ts - (ts % interval);
                    last_bucket = bucket;
                    last_offset = line_start_offset;
                    last_line = line;

                    if (last_recorded_bucket == 0) {
                        entries.push_back(TimeIndex{static_cast<uint64_t>(bucket), line_start_offset});
                        last_recorded_bucket = bucket;
                        lines_since_last = 0;
                        std::string time_str = util::format_timestamp(bucket);
                        spdlog::debug("First index entry for {}: bucket={} offset={} time={}", path, bucket, line_start_offset,time_str);
                        // continue to next line
                    } else {
                        lines_since_last++;
                        if (bucket >= static_cast<std::time_t>(last_recorded_bucket + interval) ||
                            lines_since_last >= count_threshold) {
                            entries.push_back(TimeIndex{static_cast<uint64_t>(bucket), line_start_offset});
                            last_recorded_bucket = bucket;
                            lines_since_last = 0;
                            std::string time_str = util::format_timestamp(bucket);
                            spdlog::debug("Added index entry for {}: bucket={} offset={} time={}", path, bucket, line_start_offset,time_str);
                        }
                    }
                }

                pos = nl + 1;
                processed_up_to = pos;
            }

            // remove processed prefix (keep partial line)
            if (processed_up_to > 0) carry.erase(0, processed_up_to);

            // advance total_uncompressed by n (bytes uncompressed in this chunk)
            total_uncompressed += static_cast<uint64_t>(n);
        }

        gzclose(gz);

        // apply entries to file_info
        if (!file_info.file_index) file_info.file_index = std::make_shared<FileIndex>();
        file_info.file_index->time_indexes = std::move(entries);
        double d2 = util::get_micro_timestamp();
        spdlog::info("Indexed gzip file {} entries={} skipped_lines={} time_cost={}", path, file_info.file_index->time_indexes.size(), skipped_lines,(d2-d1)/1000.0);
        
    }

    void FileIndexer::update_file_index_igzip(const std::string& path, FileInfo& file_info) {
        // parse gzip file and build time index using igzip
        double d1 = util::get_micro_timestamp();

        // Open the file using igzip
        int fd = open(path.c_str(), O_RDONLY);
        if (fd == -1) {
            return;
        }

        struct inflate_state state;
        struct isal_gzip_header gz_hdr;
        isal_gzip_header_init(&gz_hdr);

        isal_inflate_init(&state);
        state.crc_flag = ISAL_GZIP_NO_HDR_VER;  // no header verification

        const int IN_BUF_SIZE = 16 * 1024;
        const int OUT_BUF_SIZE = 16 * 1024;
        std::vector<uint8_t> in_buf(IN_BUF_SIZE);
        std::vector<uint8_t> out_buf(OUT_BUF_SIZE);

        // first read
        ssize_t bytes_read = read(fd, in_buf.data(), IN_BUF_SIZE);
        if (bytes_read <= 0) {
            close(fd);
            return;
        }
        state.next_in = in_buf.data();
        state.avail_in = static_cast<uint32_t>(bytes_read);

        // read and parse gzip header
        int ret = isal_read_gzip_header(&state, &gz_hdr);
        if (ret != ISAL_DECOMP_OK) {
            close(fd);
            return;
        }

        std::string carry; // leftover partial line from previous chunk
        std::vector<TimeIndex> entries;
        entries.reserve(1024);

        uint64_t total_uncompressed = 0;
        std::time_t last_recorded_bucket = 0;
        std::size_t lines_since_last = 0;
        const unsigned interval = index_interval_seconds_;
        const std::size_t count_threshold = index_count_threshold_;
        std::size_t skipped_lines = 0;
        
        uint64_t last_offset = 0;
        std::time_t last_bucket;
        std::string_view last_line;

        bool done = false;
        while (!done) {
            if (state.avail_in == 0) {
                bytes_read = read(fd, in_buf.data(), IN_BUF_SIZE);
                if (bytes_read <= 0) {
                    // EOF OR read error
                    done = true;
                    if (!entries.empty() && last_offset != 0) {
                        last_bucket = get_timestamp_from_log_line(last_line);
                        entries.push_back(TimeIndex{static_cast<uint64_t>(last_bucket), last_offset});
                        std::string time_str = util::format_timestamp(last_bucket);
                        spdlog::debug("Last index entry for {}: bucket={} offset={} time={}", path, last_bucket, last_offset, time_str);
                    }
                    break;
                }
                state.next_in = in_buf.data();
                state.avail_in = static_cast<uint32_t>(bytes_read);
            }
            int last_size = carry.size();
            carry.resize(last_size+OUT_BUF_SIZE);
            state.next_out = (uint8_t *)(char *)&carry[last_size];
            state.avail_out = OUT_BUF_SIZE;

            ret = isal_inflate(&state);
            if (ret != ISAL_DECOMP_OK) {
                // decompression error
                break;
            }

            size_t n = OUT_BUF_SIZE - state.avail_out;  // decompressed bytes this round
            if (n > 0) {
                //carry.append(reinterpret_cast<char*>(out_buf.data()), n);
                carry.resize(last_size+n);
                size_t previous_carry_size = carry.size() - n;
                uint64_t base_offset = total_uncompressed - previous_carry_size;

                size_t pos = 0;
                size_t processed_up_to = 0;
                while (true) {
                    size_t nl = carry.find('\n', pos);
                    if (nl == std::string::npos) break;

                    std::string_view line = std::string_view(carry).substr(pos, nl - pos);
                    uint64_t line_start_offset = base_offset + pos;

                    std::time_t ts = get_timestamp_from_log_line(line);
                    if (ts == 0) {
                        ++skipped_lines;
                    } else {
                        std::time_t bucket = ts - (ts % interval);
                        last_bucket = bucket;
                        last_offset = line_start_offset;
                        last_line = line;

                        if (last_recorded_bucket == 0) {
                            entries.push_back(TimeIndex{static_cast<uint64_t>(bucket), line_start_offset});
                            last_recorded_bucket = bucket;
                            lines_since_last = 0;
                            std::string time_str = util::format_timestamp(bucket);
                            spdlog::debug("First index entry for {}: bucket={} offset={} time={}", path, bucket, line_start_offset,time_str);
                            // continue to next line
                        } else {
                            lines_since_last++;
                            if (bucket >= last_recorded_bucket + interval ||
                                lines_since_last >= count_threshold) {
                                entries.push_back(TimeIndex{static_cast<uint64_t>(bucket), line_start_offset});
                                last_recorded_bucket = bucket;
                                lines_since_last = 0;
                                std::string time_str = util::format_timestamp(bucket);
                                spdlog::debug("Added index entry for {}: bucket={} offset={} time={}", path, bucket, line_start_offset,time_str);
                            }
                        }
                    }

                    pos = nl + 1;
                    processed_up_to = pos;
                }

                if (processed_up_to > 0) {
                    std::string tmp = carry.substr(processed_up_to);
                    carry = std::move(tmp);
                }

                total_uncompressed += n;
            }

            // if finished a gzip member
            if (state.block_state == ISAL_BLOCK_FINISH) {
                // add the last index entry of this member
                if (!entries.empty() && last_offset != 0 && n > 0) {  
                    // ensure some data was decompressed
                    last_bucket = get_timestamp_from_log_line(last_line);
                    entries.push_back(TimeIndex{static_cast<uint64_t>(last_bucket), last_offset});
                    std::string time_str = util::format_timestamp(last_bucket);
                    spdlog::debug("Last index entry for {}: bucket={} offset={} time={}", path, last_bucket, last_offset, time_str);
                }

                // check for next member
                if (state.avail_in == 0) {
                    bytes_read = read(fd, in_buf.data(), IN_BUF_SIZE);
                    if (bytes_read > 0) {
                        state.next_in = in_buf.data();
                        state.avail_in = static_cast<uint32_t>(bytes_read);
                    }
                }

                if (state.avail_in > 1 && state.next_in[0] == 31 && state.next_in[1] == 139) {
                    // found next member
                    isal_inflate_reset(&state);
                    ret = isal_read_gzip_header(&state, &gz_hdr);
                    if (ret != ISAL_DECOMP_OK) {
                        done = true;  // header error
                    }
                } else {
                    done = true;  // no more members
                }
            }
        }
        isal_inflate_reset(&state);
        close(fd);

        // Apply entries to file_info
        if (!file_info.file_index) file_info.file_index = std::make_shared<FileIndex>();
        file_info.file_index->time_indexes = std::move(entries);
        double d2 = util::get_micro_timestamp();
        spdlog::info("Indexed gzip file {} entries={} skipped_lines={} time_cost={}", path, file_info.file_index->time_indexes.size(), skipped_lines,(d2-d1)/1000.0);
    }

    std::vector<FileInfo> FileIndexer::list_prefix(const std::string& prefix) const {
        std::vector<FileInfo> out;
        std::shared_lock<std::shared_mutex> lock(mutex_);
        for (const auto& [path, info] : index_) {
            // check if prefix matches root path regex
            try {
                if (!std::regex_match(prefix, info.root_path.prefix_regex)) continue;
            } catch (const std::regex_error& e) {
                spdlog::warn("Regex error for path {}: {}", info.root_path.path, e.what());
                continue;
            }
            // check if root starts with prefix
            if(prefix.find(info.root_path.path) != 0) continue;
            // check if path starts with prefix
            if(path.find(prefix) != 0) continue;
            out.push_back(info);
        }
        return out;
    }

    std::time_t FileIndexer::get_timestamp_from_log_line(const std::string &line) {
        std::string prefix = line.substr(0, 50);
        std::smatch matches;

        for (const auto& tf : time_formats_) {
            const std::regex& pattern = tf.regex_pattern;
            const std::string& format = tf.format;
            if(std::regex_search(prefix, matches, pattern) && matches.size() > 0) {
                std::string matched_time = matches[0];
                
                // Try to parse the matched time string
                std::istringstream ss(matched_time);
                std::chrono::system_clock::time_point tp;
                // date::parse instead std::get_time
                ss >> date::parse(format, tp);
                
                if (!ss.fail()) {
                    return std::chrono::duration_cast<std::chrono::seconds>(
                        tp.time_since_epoch()).count();
                }
            }
        }
        return 0; // Return 0 if no valid timestamp found
    }

    std::time_t FileIndexer::get_timestamp_from_log_line(const std::string_view &line) {
        std::string prefix(line.substr(0, 50));
        std::smatch matches;

        for (const auto& tf : time_formats_) {
            const std::regex& pattern = tf.regex_pattern;
            const std::string& format = tf.format;
            if(std::regex_search(prefix, matches, pattern) && matches.size() > 0) {
                std::string matched_time = matches[0];
                
                // Try to parse the matched time string
                std::istringstream ss(matched_time);
                std::chrono::system_clock::time_point tp;
                // date::parse instead std::get_time
                ss >> date::parse(format, tp);
                
                if (!ss.fail()) {
                    return std::chrono::duration_cast<std::chrono::seconds>(
                        tp.time_since_epoch()).count();
                }
            }
        }
        return 0; // Return 0 if no valid timestamp found
    }

    void FileIndexer::remove_unused_indexes() {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        for (auto it = index_.begin(); it != index_.end(); ) {
            //remove non-indexed files
            if (!it->second.file_index || it->second.file_index->time_indexes.empty()) {
                //spdlog::info("Removing unused index for file: {}", it->first);
                //it = index_.erase(it);
                //continue;
                //keep the empty index file
                ++it;
                continue;
            }
            if (!fs::exists(it->first)) {
                //remove deleted files
                spdlog::info("Removing index for deleted file: {}", it->first);
                it = index_.erase(it);
                continue;
            }
            ++it;
        }
    }

    void FileIndexer::save_index_to_cache() {
        try {
            if (updated_index_count_ == 0) {
                spdlog::info("No updated indexes, skipping cache save");
                return;
            }
            // ensure cache dir exists
            if (!cache_path_.empty()) fs::create_directories(cache_path_);

            fs::path target = fs::path(cache_path_) / ".index_cache.json";
            fs::path tmp = target.string() + ".tmp";

            // make a snapshot copy of index_ under lock, then release lock
            std::unordered_map<std::string, FileInfo> snapshot;
            {
                std::shared_lock<std::shared_mutex> lock(mutex_);
                snapshot = index_; // copy
            }

            json j = json::array();
            for (const auto &kv : snapshot) {
                const FileInfo &fi = kv.second;
                json o;
                o["fullpath"] = fi.fullpath;
                o["name"] = fi.name;
                o["dir"] = fi.dir;
                o["size"] = fi.size;
                o["mtime"] = fi.mtime;
                o["ftype"] = fi.file_type;
                o["etag"] = fi.etag;
                o["inode"] = fi.inode;
                // root path -> only store path string (regex not serializable)
                o["root_path"] = fi.root_path.path;
                if (fi.file_index) {
                    json idx;
                    idx["index_etag"] = fi.file_index->index_etag;
                    idx["last_index_time"] = fi.file_index->last_index_time;
                    idx["time_indexes"] = json::array();
                    for (const auto &ti : fi.file_index->time_indexes) {
                        json it;
                        it["timestamp"] = ti.timestamp;
                        it["offset"] = ti.offset;
                        idx["time_indexes"].push_back(it);
                    }
                    o["file_index"] = idx;
                }
                j.push_back(o);
            }

            // write to temp file
            std::ofstream ofs(tmp, std::ios::binary | std::ios::trunc);
            if (!ofs.is_open()) {
                spdlog::error("Failed to open cache temp file for writing: {}", tmp.string());
                return;
            }
            ofs << j.dump(2);
            ofs.close();

            // atomic replace
            std::error_code ec;
            fs::rename(tmp, target, ec);
            if (ec) {
                // fallback to std::rename
                if (std::rename(tmp.string().c_str(), target.string().c_str()) != 0) {
                    spdlog::error("Failed to move cache temp file {} to {}: {}", tmp.string(), target.string(), ec.message());
                }
            } else {
                spdlog::info("Wrote index cache to {}", target.string());
            }
            spdlog::info("Index cache saved, entries={}", snapshot.size());
        } catch (const std::exception &e) {
            spdlog::error("Exception in save_index_to_cache: {}", e.what());
        }
    }

    void FileIndexer::load_index_from_cache() {
        try {
            fs::path target = fs::path(cache_path_) / ".index_cache.json";
            if (!fs::exists(target)) {
                spdlog::debug("Cache file not found: {}", target.string());
                return;
            }
            std::ifstream ifs(target, std::ios::binary);
            if (!ifs.is_open()) {
                spdlog::error("Failed to open cache file for reading: {}", target.string());
                return;
            }
            json j;
            try {
                ifs >> j;
            } catch (const std::exception &e) {
                spdlog::error("Failed to parse cache JSON {}: {}", target.string(), e.what());
                return;
            }

            //make root path to temp map from roots_
            std::unordered_map<std::string, RootPath> root_paths;
            for (const auto &rp : roots_) {
                root_paths.emplace(rp.path, rp);
            }

            // build temporary map then swap in under lock
            std::unordered_map<std::string, FileInfo> tmp_index;
            for (const auto &o : j) {
                try {
                    FileInfo fi;
                    fi.fullpath = o.value("fullpath", std::string());
                    std::string rp = o.value("root_path", std::string());
                    // set regex from root_paths map
                    if (root_paths.find(rp) == root_paths.end() || index_.find(fi.fullpath) == index_.end()) {
                        spdlog::warn("Skipping cache entry with unknown root path or past by filter: {}", fi.fullpath);
                        continue;
                    }
                    fi.root_path = root_paths[rp];
                    fi.name = o.value("name", std::string());
                    fi.dir = o.value("dir", std::string());
                    fi.size = o.value("size", std::uint64_t(0));
                    fi.mtime = o.value("mtime", std::time_t(0));
                    fi.file_type = o.value("ftype", std::string());
                    fi.etag = o.value("etag", std::string());
                    fi.inode = o.value("inode", uint64_t(0));
        
                    fi.root_path.path_regex = std::regex(fi.root_path.path_pattern);
                    fi.root_path.filename_regex = std::regex(fi.root_path.filename_pattern);
                    fi.root_path.time_format_regex = std::regex(fi.root_path.time_format_pattern);
                    fi.root_path.prefix_regex = std::regex(fi.root_path.prefix_pattern);
                    
                    if (o.contains("file_index")) {
                        const auto &idx = o["file_index"];
                        auto pfi = std::make_shared<FileIndex>();
                        pfi->index_etag = idx.value("index_etag", std::string());
                        pfi->last_index_time = idx.value("last_index_time", std::time_t(0));
                        if (idx.contains("time_indexes") && idx["time_indexes"].is_array()) {
                            for (const auto &it : idx["time_indexes"]) {
                                TimeIndex ti;
                                ti.timestamp = it.value("timestamp", uint64_t(0));
                                ti.offset = it.value("offset", uint64_t(0));
                                pfi->time_indexes.push_back(ti);
                            }
                        }
                        fi.file_index = std::move(pfi);
                    }
                    if (!fi.fullpath.empty()) tmp_index.emplace(fi.fullpath, std::move(fi));
                } catch (const std::exception &e) {
                    spdlog::warn("Skipping invalid cache entry: {}", e.what());
                }
            }

            // swap into live index under write lock
            {
                std::unique_lock<std::shared_mutex> lock(mutex_);
                index_.swap(tmp_index);
            }
            spdlog::info("Loaded index cache from {}, entries={}", target.string(), index_.size());
        } catch (const std::exception &e) {
            spdlog::error("Exception in load_index_from_cache: {}", e.what());
        }
    }

    bool FileIndexer::get_file_index_by_path(const std::string& path, FileInfo& out_info) const {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        auto it = index_.find(path);
        if (it != index_.end()) {
            out_info = it->second;
            return true;
        }
        return false;
    }

} // namespace drlog