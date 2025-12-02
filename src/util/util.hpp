#pragma once
#include <string>
#include <cstdint>
#include <ctime>
#include <vector>
#include <regex>

namespace drlog {
    class util {
    public:
        // compute 64-bit MurMurHash (user provided implementation)
        static unsigned long long MurMurHash64(const void* ptr, unsigned long long len, unsigned long long seed);

        // build etag string from file size and mtime using MurMurHash64 then hex encode
        static std::string etag_from_size_mtime(std::uint64_t size, std::time_t mtime);
        static std::string format_timestamp(std::time_t ts, const std::string& format = "%Y-%m-%d %H:%M:%S");
        static bool gzip_compress(const std::string& data, std::string& compressed_data);
        static bool gzip_decompress(const std::string& compressed_data, std::string& decompressed_data);
        static std::string url_decode(const std::string& encoded_url);
        static std::string url_encode(const std::string& url);
        static std::vector<std::string> split(const std::string& str, char delimiter);
    };
} // namespace util
