#include "igzip.hpp"
namespace drlog
{
    #define GZIP_DEFAULT_BUF_SIZE 16*1024
    int igzip::igzopen(const char* filename, const char* mode, igzip_state* igz) {
        if(!igz) {
            return -1001; // Invalid state pointer
        }

        igz->in_file = fopen(filename, mode);
        if (!igz->in_file) {

            return -101; // Failed to open file
        }
        int ret = 0;
        igz->in_buf_size = GZIP_DEFAULT_BUF_SIZE;
        igz->in_buffer.resize(igz->in_buf_size);
        isal_gzip_header_init(&igz->gz_hdr);
        isal_inflate_init(&igz->state);
        igz->state.crc_flag = ISAL_GZIP_NO_HDR_VER;  // no header verification
        igz->state.next_in = igz->in_buffer.data();
        igz->state.avail_in = fread(igz->in_buffer.data(), 1, igz->in_buf_size, igz->in_file);
        if(ferror(igz->in_file)) {
            fclose(igz->in_file);
            igz->in_file = nullptr;
            return -102; // Error reading file
        }
        ret = isal_read_gzip_header(&igz->state, &igz->gz_hdr);
        if (ret != ISAL_DECOMP_OK) {
            fclose(igz->in_file);
            igz->in_file = nullptr;
            return ret; // Error reading gzip header
        }
        return 0; // Success
    }
    
    int igzip::igzread(igzip_state* igz, std::vector<uint8_t> &buf) {
        if(!igz || !igz->in_file) {
            return -1001; // // Invalid state pointer
        }
        if (buf.data() == nullptr || buf.size() == 0) {
            return -1002; // Invalid buffer or length
        }
        if(igz->state.block_state == ISAL_BLOCK_FINISH) {
            return 0; // End of stream
        }
        int ret = 0;
        if (igz->state.avail_in == 0) {
            igz->state.next_in = igz->in_buffer.data();
            igz->state.avail_in = fread(igz->in_buffer.data(), 1, igz->in_buf_size, igz->in_file);
            if (ferror(igz->in_file)) {
                return -102; // Error reading file
            }
        }

        igz->state.next_out = buf.data();
        igz->state.avail_out = static_cast<uint32_t>(buf.size());

        ret = isal_inflate(&igz->state);
        if (ret != ISAL_DECOMP_OK && ret < 0 ) {
            // Decompression error
            return ret;
        }
        return static_cast<int>(buf.size() - igz->state.avail_out); // Return number of bytes read
    }

    int igzip::igzclose(igzip_state* igz) {
        if (!igz || !igz->in_file) {
            return -1001; // Invalid state pointer
        }
        fclose(igz->in_file);
        igz->in_file = nullptr;
        return 0; // Success
    }
} // namespace drlog

