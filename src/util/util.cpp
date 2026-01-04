#include "util.hpp"
#include <sstream>
#include <iomanip>
#include <cstring>
#include <zlib.h>
#include <spdlog/spdlog.h>
#include <chrono>

namespace drlog {

    static constexpr unsigned long long DEFAULT_MURMUR_SEED = 3339675888ULL;

    unsigned long long util::MurMurHash64(const void* ptr, unsigned long long len, unsigned long long seed)
    {
        const unsigned long long mul = (0xc6a4a793ULL << 32ULL) + 0x5bd1e995ULL;
        const char* const buf = static_cast<const char*>(ptr);
        
        const int len_aligned = static_cast<int>(len & ~0x7);
        const char* const end = buf + len_aligned;
        unsigned long long hash = seed ^ (len * mul);
        for (const char* p = buf; p != end; p += 8)
        {
            // read 8 bytes as little-endian unsigned long long
            unsigned long long data;
            std::memcpy(&data, p, sizeof(data));
            data *= mul;
            data = (data ^ (data >> 47)) * mul;
            hash ^= data;
            hash *= mul;
        }
        if ((len & 0x7) != 0)
        {
            unsigned long long data = 0;
            int n = (len & 0x7) - 1;
            do {
                data = (data << 8) + static_cast<unsigned char>(end[n]);
            } while (--n >= 0);
            hash ^= data;
            hash *= mul;
        }
        hash = (hash ^ (hash >> 47)) * mul;
        hash = hash ^ (hash >> 47);
        return hash;
    }

    std::string util::etag_from_size_mtime(std::uint64_t size, std::time_t mtime) {
        // pack size (8 bytes) and mtime (8 bytes) into buffer
        unsigned char buf[16];
        // ensure consistent little-endian packing
        std::uint64_t s = size;
        std::uint64_t t = static_cast<std::uint64_t>(mtime);
        std::memcpy(buf, &s, sizeof(s));
        std::memcpy(buf + sizeof(s), &t, sizeof(t));
        unsigned long long hash = MurMurHash64(buf, sizeof(buf), DEFAULT_MURMUR_SEED);
        std::ostringstream oss;
        oss << std::hex << std::nouppercase << std::setfill('0') << std::setw(16) << hash;
        return oss.str();
    }

    std::string util::format_timestamp(std::time_t ts, const std::string& format) {
            std::tm tm{};
            // use thread-safe localtime_r when available
        #if defined(_POSIX_THREADS)
            if (localtime_r(&ts, &tm) == nullptr) return "";
        #else
            auto tptr = std::localtime(&ts);
            if (!tptr) return "";
            tm = *tptr;
        #endif
            std::ostringstream oss;
            oss << std::put_time(&tm, format.c_str());
            return oss.str();
    }

    bool util::gzip_compress(const std::string& data, std::string& compressed_data) {
        z_stream strm = {};
        int ret = deflateInit2(&strm, Z_BEST_COMPRESSION, Z_DEFLATED, 16 + MAX_WBITS, 8, Z_DEFAULT_STRATEGY);  
        if (ret != Z_OK) {
            spdlog::error("deflateInit2 failed with error code: {}", ret);
            return false;
        }

        strm.avail_in = data.size();
        strm.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(data.data()));

        compressed_data.resize(data.size()); 
        strm.avail_out = compressed_data.size();
        strm.next_out = reinterpret_cast<Bytef*>(compressed_data.data());

        ret = deflate(&strm, Z_FINISH);
        if (ret != Z_STREAM_END) {
            deflateEnd(&strm);
            spdlog::error("deflate failed with error code: {}", ret);
            return false;
        }

        compressed_data.resize(strm.total_out);  
        deflateEnd(&strm);
        return true;
    }

    bool util::gzip_decompress(const std::string& compressed_data, std::string& decompressed_data) {
        z_stream strm = {};
        int ret = inflateInit2(&strm, 16 + MAX_WBITS);
        if (ret != Z_OK) {
            spdlog::error("inflateInit2 failed with error code: {}", ret);
            return false;
        }
        
        strm.avail_in = compressed_data.size();
        strm.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(compressed_data.data()));
        
        // Decompress in chunks
        const size_t CHUNK_SIZE = 65536;  // 64KB
        std::vector<char> buffer(CHUNK_SIZE);
        
        do {
            strm.avail_out = buffer.size();
            strm.next_out = reinterpret_cast<Bytef*>(buffer.data());
            
            ret = inflate(&strm, Z_NO_FLUSH);
            if (ret != Z_OK && ret != Z_STREAM_END) {
                inflateEnd(&strm);
                spdlog::error("inflate failed with error code: {}", ret);
                return false;
            }
            
            // Append decompressed data to output string
            size_t have = buffer.size() - strm.avail_out;
            decompressed_data.append(buffer.data(), have);
            
        } while (strm.avail_out == 0);
        
        inflateEnd(&strm);
        return ret == Z_STREAM_END;
    }

    std::string util::url_decode(const std::string &encoded_url) {
        std::string decoded_url;
        for (size_t i = 0; i < encoded_url.length(); ++i) {
            if (encoded_url[i] == '%' && i + 2 < encoded_url.length()) {
                int value;
                std::stringstream ss;
                ss << std::hex << encoded_url.substr(i + 1, 2);
                ss >> value;
                decoded_url += static_cast<char>(value);
                i += 2;  // Skip the next two characters (hex value)
            } else {
                decoded_url += encoded_url[i];
            }
        }
        return decoded_url;
    }

    std::string util::url_encode(const std::string& url) {
        std::ostringstream encoded_url;
        for (unsigned char c : url) {
            if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
                encoded_url << c;
            } else {
                encoded_url << '%' << std::uppercase << std::setw(2) << std::setfill('0') << std::hex << static_cast<int>(c);
            }
        }
        return encoded_url.str();
    }
    
    std::vector<std::string> util::split(const std::string& str, char delimiter) {
        std::vector<std::string> tokens;
        std::stringstream ss(str);
        std::string token;
        while (std::getline(ss, token, delimiter)) {
            tokens.push_back(token);
        }
        return tokens;
    }

    double util::get_micro_timestamp() {
        return std::chrono::duration<double, std::micro>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count();
    }

    int util::get_local_utc_offset_seconds() {
        auto now = std::chrono::system_clock::now();
        auto local_time = std::chrono::zoned_time<std::chrono::seconds>(
            std::chrono::current_zone(), 
            std::chrono::time_point_cast<std::chrono::seconds>(now));
    
        auto utc_time = std::chrono::zoned_time<std::chrono::seconds>(
            "UTC", 
            std::chrono::time_point_cast<std::chrono::seconds>(now));
        
        auto diff = local_time.get_local_time() - utc_time.get_local_time();
        return static_cast<int>(diff.count());
    }
} // namespace util
