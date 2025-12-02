#include "searcher.hpp"
#include <fstream>
#include <regex>
#include <string>
#include <zlib.h>
#include <spdlog/spdlog.h>
#include <iostream>
#include "searchers/simple_searcher.hpp"
#include "searchers/boolean_searcher.hpp"
#include "searchers/regex_searcher.hpp"

namespace drlog {

    #define MAX_CARRY_SIZE (1024 * 1024 * 100)
    #define MAX_BATCH_MATCHES (500)
    // Execute searchers on lines in context
    bool LogSearcher::exec_searchers(std::shared_ptr<SearchContext> ctx) {
        if(!ctx || ctx->searchers.empty()) {
            spdlog::error("Search context or searchers is not valid");
            return false;
        }
        int matched_count = 0;
        std::vector<matched_word> matched;
        for(auto &line : ctx->log_lines) {
            bool be_matched = true;
            for(auto &qs : ctx->searchers) {
                std::vector<matched_word>().swap(matched);
                bool besucc = qs.searcher->search_line(line.line, matched, false);
                if(!besucc) {
                    be_matched = false;
                    break;
                }
            }
            if(be_matched) {
                matched_count++;
                ctx->matched_lines.emplace_back(std::move(line));
            }
        }
        spdlog::debug("File '{}',search {} lines, matched {} lines, total matched {} lines", 
            ctx->path,ctx->log_lines.size(), matched_count, ctx->matched_lines.size());
        return true;
    }
   
    bool LogSearcher::parse_line(std::shared_ptr<SearchContext> ctx,std::string &line) {
        if(!indexer_) {
            spdlog::error("Indexer is not initialized");
            return false;
        }
        //get timestamp of line
        std::time_t ts = indexer_->get_timestamp_from_log_line(line);
        if(ts == 0) {
            //this not a new log line
            if(!ctx->tmp_line.line.empty()) {
                // have one line header, append it
                ctx->tmp_line.line.append("\n").append(line);
            } else {
                // no line header, skip
            }
        }
        else {
            if(ts < ctx->start_time) {
                // timestamp out of range, skip
                return true;
            }
            if(!ctx->tmp_line.line.empty()) {
                // have one full log line, append it
                LogLine ll;
                ll.line.swap(ctx->tmp_line.line);
                ll.timestamp = ctx->tmp_line.timestamp;
                ctx->log_lines.emplace_back(std::move(ll));
            }
            if(ts > ctx->end_time) {
                // timestamp out of range, skip
                return true;
            }
            ctx->tmp_line.line.swap(line);
            ctx->tmp_line.timestamp = ts;
            std::string().swap(line);
        }
        return true;
    }

    void LogSearcher::search_file_txt(std::shared_ptr<SearchContext> ctx) {
        std::shared_ptr<FileInfo> file_index = ctx->index_file_info;
        std::shared_ptr<SearchRequest> req = ctx->req;
        const std::string &path = ctx->path;
        std::ifstream ifs(path, std::ios::in);
        if (!ifs.is_open())
        {
            spdlog::error("Failed to open file {} for reading", path);
            ctx->error_msg = "Failed to open file for reading";
            ctx->status = 1;
            return;
        }
        //get file lenth
        ifs.seekg(0, std::ios::end);
        uint64_t file_size = ifs.tellg();
        if(ctx->index_start_pos >= file_size) {
            ctx->error_msg = "Index start position is out of file range";
            ctx->status = 1;
            return;
        }
        if(ctx->index_end_pos >= file_size) {
            ctx->error_msg = "Index end position is out of file range";
            ctx->status = 1;
            return;
        }
        ifs.seekg(static_cast<std::streamoff>(ctx->index_start_pos));
        std::string line;
        try {
            //read lines until end_pos
            while (std::getline(ifs,line)) {
                //match the commands
                bool besucc = parse_line(ctx, line);
                if(!besucc) break;
                if(ifs.tellg() > ctx->index_end_pos) break;
                if(ctx->log_lines.size() >= MAX_BATCH_MATCHES)
                {
                    //search the lines
                    besucc = exec_searchers(ctx);
                    std::vector<LogLine>().swap(ctx->log_lines);
                    if(!besucc) {
                        spdlog::error("Failed to execute searchers for file {}", path);
                        ctx->error_msg = "Failed to execute searchers for file";
                        ctx->status = 1;
                        break;
                    }
                    if (ctx->matched_lines.size() >= req->max_results) {
                        break;
                    }
                }
           }
           if(!ctx->tmp_line.line.empty()) {
                LogLine ll;
                ll.line.swap(ctx->tmp_line.line);
                ll.timestamp = ctx->tmp_line.timestamp;
                ctx->log_lines.emplace_back(std::move(ll));
           }
           if(!ctx->log_lines.empty()) {
                //search the lines
                bool besucc = exec_searchers(ctx);
                if(!besucc) {
                    spdlog::error("Failed to execute searchers for file {}", path);
                    ctx->error_msg = "Failed to execute searchers for file";
                    ctx->status = 1;
                    return;
                }
                std::vector<LogLine>().swap(ctx->log_lines);
           }
        } catch (const std::exception& e) {
            spdlog::error("Error reading file {}: {}", path, e.what());
            ctx->error_msg = "Error reading file for reading";
            ctx->status = 1;
            return;
        } catch (...) {
            spdlog::error("Unknown error reading file {}", path);
            ctx->error_msg = "Unknown error reading file for reading";
            ctx->status = 1;
            return;
        }
        ifs.close();
    }

    void LogSearcher::get_lines_gzip(std::string &carry,std::vector<std::string> &lines,size_t pos)
    {
        std::string line;
        while (pos < carry.size()) {
            size_t nl = carry.find('\n', pos);
            // No newline found or reached end of file
            if (nl == std::string::npos) {
                break;
            }
            line = carry.substr(pos, nl - pos);
            lines.emplace_back(std::move(line));
            pos = nl + 1;
        }
        
        // Remove processed parts
        if (pos > 0 && pos < carry.size()) {
            carry = carry.substr(pos);
            return;
        }
        if(pos >= carry.size()) {
            std::string().swap(carry);
            return;
        }
        if(carry.size() > MAX_CARRY_SIZE) {
            spdlog::error("Carry size {} is too large", carry.size());
            return;
        }
    }

    void LogSearcher::search_file_gzip(std::shared_ptr<SearchContext> ctx) {
        std::shared_ptr<FileInfo> file_index = ctx->index_file_info;
        const std::string &path = ctx->path;
        std::shared_ptr<SearchRequest> req = ctx->req;
        // Open gzip file
        gzFile gz = gzopen(path.c_str(), "rb");
        if (!gz) {
            spdlog::error("Failed to open gzip file {} for reading", path);
            ctx->error_msg = "Failed to open gzip file for reading";
            ctx->status = 1;
            return;
        } 
        try {
            // Set up buffer
            const int BUF_SIZE = 8192;
            std::vector<char> buffer(BUF_SIZE);
            std::string carry;
            uint64_t total_uncompressed = 0;
            
            // Precisely position to start position
            while (total_uncompressed < ctx->index_start_pos) {
                // Calculate remaining bytes to skip
                uint64_t remaining = ctx->index_start_pos - total_uncompressed;
                // Read size not exceeding buffer size and remaining bytes
                int read_size = std::min(BUF_SIZE, static_cast<int>(remaining));
                int n = gzread(gz, buffer.data(), read_size);
                if (n < 0) {
                    int err;
                    const char* err_msg = gzerror(gz, &err);
                    spdlog::error("Error reading gzip file {}: {}", path, err_msg);
                    ctx->error_msg = "Error reading gzip file";
                    ctx->status = 1;
                    gzclose(gz);
                    return;
                } 
                if (n == 0) {
                    ctx->error_msg = "Index start position is out of file range";
                    ctx->status = 1;
                    gzclose(gz);
                    return;
                }
                total_uncompressed += static_cast<uint64_t>(n);
                // If we didn't read enough bytes, file might have ended
                if (n < read_size) {
                    ctx->error_msg = "Index start position is out of file range";
                    ctx->status = 1;
                    gzclose(gz);
                    return;
                }
            }
            
            // Ensure we reached the exact start position
            if (total_uncompressed != ctx->index_start_pos) {
                ctx->error_msg = "Could not reach exact start position in gzip file";
                ctx->status = 1;
                gzclose(gz);
                return;
            }
            
            // Read and process file content until end position
            while (total_uncompressed < ctx->index_end_pos) {
                int n = gzread(gz, buffer.data(), BUF_SIZE);
                if (n < 0) {
                    int err;
                    const char* err_msg = gzerror(gz, &err);
                    spdlog::error("Error reading gzip file {}: {}", path, err_msg);
                    ctx->error_msg = "Error reading gzip file";
                    ctx->status = 1;
                    gzclose(gz);
                    return;
                }
                if (n == 0) {
                    // Reached end of file before end position
                    break;
                }
                
                carry.append(buffer.data(), n);
                total_uncompressed += static_cast<uint64_t>(n);
                
                // Get lines from carry
                std::vector<std::string> lines;
                get_lines_gzip(carry,lines,0);
                bool befaild = false;
                for(auto &line : lines) {
                    bool besucc = parse_line(ctx, line);
                    if(!besucc) {
                        befaild = true;
                        break;
                    }
                    if(ctx->log_lines.size() >= MAX_BATCH_MATCHES) {
                        //search the line
                        besucc = exec_searchers(ctx);
                        std::vector<LogLine>().swap(ctx->log_lines);
                        if(!besucc) {
                            spdlog::error("Failed to execute searchers for file {}", path);
                            ctx->error_msg = "Failed to execute searchers for file";
                            ctx->status = 1;
                            gzclose(gz);
                            return;
                        }
                        if (ctx->matched_lines.size() >= req->max_results) {
                            break;
                        }
                    }
                }
                if(befaild) {
                    spdlog::error("Failed to parse line for file {}", path);
                    break;
                }
                std::vector<std::string>().swap(lines);
                // Check if exceeded end position
                if (total_uncompressed >= ctx->index_end_pos) {
                    break;
                }
            }
            if(!ctx->tmp_line.line.empty()) {
                    LogLine ll;
                    ll.line.swap(ctx->tmp_line.line);
                    ll.timestamp = ctx->tmp_line.timestamp;
                    ctx->log_lines.emplace_back(std::move(ll));
            }
            if(!ctx->log_lines.empty()) {
                    //match the lines
                    bool besucc = exec_searchers(ctx);
                    if(!besucc) {
                        spdlog::error("Failed to execute searchers for file {}", path);
                        ctx->error_msg = "Failed to execute searchers for file";
                        ctx->status = 1;
                        return;
                    }
                    std::vector<LogLine>().swap(ctx->log_lines);
            }
            
        } catch (const std::exception& e) {
            spdlog::error("Error processing gzip file {}: {}", path, e.what());
            ctx->error_msg = "Error processing gzip file";
            ctx->status = 1;
        } catch (...) {
            spdlog::error("Unknown error processing gzip file {}", path);
            ctx->error_msg = "Unknown error processing gzip file";
            ctx->status = 1;
        }
        gzclose(gz);
    }

    void LogSearcher::search_file(std::shared_ptr<SearchContext> ctx) {
        std::string type = ctx->index_file_info->file_type;
        if (type == "gzip") {
            search_file_gzip(ctx);
        } else {
            search_file_txt(ctx);
        }
    }

    bool LogSearcher::find_index_pos(std::shared_ptr<SearchContext> ctx) {
        // Find index position
        if (!ctx || !ctx->index_file_info || !ctx->index_file_info->file_index) {
            spdlog::warn("Index file info or file index is not valid for path '{}'", ctx->path);
            return false;
        }

        const auto& time_indexes = ctx->index_file_info->file_index->time_indexes;
        if (time_indexes.empty()) {
            spdlog::warn("Time indexes is empty for path '{}'", ctx->path);
            return false;
        }

        // Find the first index entry >= start_time
        int start_idx = 0x7FFFFFFF;
        int end_idx = -1;
        bool be_fount = false;
        for (int i = 0; i < time_indexes.size() - 1; ++i) {
            uint64_t idx_start_time = time_indexes[i].timestamp;
            uint64_t idx_end_time = time_indexes[i + 1].timestamp;
            if (timestamp_covers(idx_start_time,idx_end_time, ctx->start_time, ctx->end_time)) {
                if (i < start_idx) start_idx = i;
                if (i + 1 > end_idx) end_idx = i + 1;
                be_fount = true;
            }
        }
        
        if (!be_fount || start_idx == 0x7FFFFFFF || end_idx == -1) {
            spdlog::warn("No index entry covers the time range for path '{}', start_time={}, end_time={}, start_idx={}, end_idx={}",
                 ctx->path, ctx->start_time, ctx->end_time, start_idx, end_idx);
            return false;
        }

        ctx->index_start_time = time_indexes[start_idx].timestamp;
        ctx->index_start_pos = time_indexes[start_idx].offset;
        ctx->index_end_time = time_indexes[end_idx].timestamp;
        ctx->index_end_pos = time_indexes[end_idx].offset;

        return true;
    }

    bool LogSearcher::build_searchers(std::shared_ptr<SearchContext> ctx) {
        if(!ctx || !ctx->req) {
            spdlog::warn("Search context or request is null");
            return false;
        }
        if(ctx->searchers.size() > 0) {
            // already built
            return true;
        }
        for(auto &q : ctx->req->queries) {
            if(q.query.empty()) {
                spdlog::warn("Query string is empty");
                return false;
            }
            QuerySearcher qs;
            qs.query_string = q;
            if(q.type == "simple") {
                qs.type = SearchType::SIMPLE;
                qs.searcher = std::make_shared<simple_searcher>();
                if(!qs.searcher->build_pattern(q.query)) {
                    spdlog::warn("Failed to build simple searcher for query '{}'", q.query);
                    return false;
                }
                ctx->searchers.emplace_back(std::move(qs));
            } else if(q.type == "boolean") {
                qs.type = SearchType::BOOL;
                qs.searcher = std::make_shared<boolean_searcher>();
                if(!qs.searcher->build_pattern(q.query)) {
                    spdlog::warn("Failed to build boolean searcher for query '{}'", q.query);
                    return false;
                }
                ctx->searchers.emplace_back(std::move(qs));
            } else if(q.type == "regex") {
                qs.type = SearchType::REGEX;
                qs.searcher = std::make_shared<regex_searcher>();
                if(!qs.searcher->build_pattern(q.query)) {
                    spdlog::warn("Failed to build regex searcher for query '{}'", q.query);
                    return false;
                }
                ctx->searchers.emplace_back(std::move(qs));
            } else {
                spdlog::warn("Unknown query type '{}'", q.type);
                return false;
            }
        }
        return true;
    }

    void LogSearcher::search(const SearchRequest& req, SearchResult& result) {
        // Validate queries
        if (req.queries.empty()) {
            spdlog::warn("Search request has no queries");
            result.status = 1;
            result.error_msg = "No queries specified";
            return;
        }
        if (req.paths.empty()) {
            spdlog::warn("Search request has no target paths");
            result.status = 1;
            result.error_msg = "No target paths specified";
            return;
        }
        if (indexer_ == nullptr) {
            spdlog::warn("Indexer is not valid");
            result.status = 1;
            result.error_msg = "Indexer is not valid";
            return;
        }
        // Iterate target paths
        for (const auto &p : req.paths) {
            std::shared_ptr<SearchContext> ctx = std::make_shared<SearchContext>();
            ctx->req = std::make_shared<SearchRequest>(req);
            ctx->path = p;
            ctx->start_time = req.start_time;
            ctx->end_time = req.end_time;
            FileInfo fi;
            FileMatches fm;
            bool besucc = build_searchers(ctx);
            if(!besucc) {
                spdlog::warn("Failed to build searchers for path '{}'", p);
                result.status = 1;
                result.error_msg = "Failed to build searchers patterns";
                return;
            }
            bool befind = indexer_->get_file_index_by_path(p, fi);
            if (!befind || !fi.file_index) {
                fm.path = p;
                fm.status = 1;
                fm.error_msg = "File not found in index list";
                result.matches.push_back(std::make_shared<FileMatches>(std::move(fm)));
                spdlog::warn("Path '{}' not found in index list", p);
                continue;
            }
            ctx->index_file_info = std::make_shared<FileInfo>(fi);
            if (!find_index_pos(ctx)) {
                fm.path = p;
                fm.status = 1;
                fm.error_msg = "Time range not covered by index";
                result.matches.push_back(std::make_shared<FileMatches>(std::move(fm)));
                spdlog::warn("Path '{}' has no index", p);
                continue;
            }
            search_file(ctx);
            fm.path = p;
            fm.status = ctx->status;
            fm.error_msg = ctx->error_msg;
            fm.lines = std::move(ctx->matched_lines);
            result.matches.push_back(std::make_shared<FileMatches>(std::move(fm)));
            if(ctx->status != 0)
                spdlog::warn("Search failed for path '{}': {}", p, ctx->error_msg);
        }
    }
    bool LogSearcher::timestamp_covers(uint64_t idx_satrt, uint64_t idx_end, uint64_t start_time, uint64_t end_time) {
        return (idx_satrt <= end_time && idx_end >= start_time);
    }
} // namespace drlog