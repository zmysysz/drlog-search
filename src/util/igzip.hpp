
#include "libs/igzip/igzip_lib.h"
#include <vector>
#include <stdio.h>

namespace drlog {
    struct igzip_state {
        struct inflate_state state;
        struct isal_gzip_header gz_hdr;
        std::vector<uint8_t> in_buffer;
        size_t in_buf_size;
        FILE *in_file;
        char *filename;
        char *mode;

        igzip_state(){
        }

        ~igzip_state()  {
            if (in_file) {
                fclose(in_file);
                in_file = nullptr;
            }
        }
    };

    class igzip
    {
    private:
        /* data */
    public:
        igzip(/* args */){};
        ~igzip(){};
        static int igzopen(const char* filename, const char* mode, igzip_state* igz);
        static int igzread(igzip_state* igz, std::vector<uint8_t> &buf);
        static int igzclose(igzip_state* igz);
    };
} // namespace drlog
