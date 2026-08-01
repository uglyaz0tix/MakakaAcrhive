#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <filesystem>
#include <lzma.h>
#include <zstd.h>

namespace fs = std::filesystem;

constexpr uint32_t MAKAKA_SIGNATURE = 0x4D4B4B41;
constexpr uint16_t MAKAKA_VERSION = 0x0100;

enum Method {
    M_NONE = 0,
    M_LZMA = 1,
    M_ZSTD = 2
};

std::vector<uint8_t> wasdwasd(const std::vector<uint8_t>& src) {
    lzma_stream strm = LZMA_STREAM_INIT;
    if (lzma_easy_encoder(&strm, 6, LZMA_CHECK_CRC64) != LZMA_OK) {
        throw std::runtime_error("lzma init failed");
    }

    strm.next_in = src.data();
    strm.avail_in = src.size();

    std::vector<uint8_t> out(src.size() / 2 + 1024);
    strm.next_out = out.data();
    strm.avail_out = out.size();

    while (true) {
        lzma_ret ret = lzma_code(&strm, LZMA_FINISH);
        if (ret == LZMA_STREAM_END) {
            break;
        }
        if (ret != LZMA_OK) {
            lzma_end(&strm);
            throw std::runtime_error("lzma failed during compression");
        }
        if (strm.avail_out == 0) {
            size_t old_size = out.size();
            out.resize(old_size * 2);
            strm.next_out = out.data() + old_size;
            strm.avail_out = out.size() - old_size;
        }
    }

    out.resize(strm.total_out);
    lzma_end(&strm);
    return out;
}

std::vector<uint8_t> stendof2(const std::vector<uint8_t>& src) {
    size_t bound = ZSTD_compressBound(src.size());
    std::vector<uint8_t> out(bound);
    size_t size = ZSTD_compress(out.data(), out.size(), src.data(), src.size(), 3);
    
    if (ZSTD_isError(size)) {
        throw std::runtime_error(ZSTD_getErrorName(size));
    }
    out.resize(size);
    return out;
}

void kakashki(const std::vector<std::string>& files, const std::string& dst, Method m) {
    std::ofstream f(dst, std::ios::binary);
    if (!f) throw std::runtime_error("can't create archive file");

    f.write(reinterpret_cast<const char*>(&MAKAKA_SIGNATURE), 4);
    f.write(reinterpret_cast<const char*>(&MAKAKA_VERSION), 2);
    
    uint16_t cm = static_cast<uint16_t>(m);
    f.write(reinterpret_cast<const char*>(&cm), 2);

    uint32_t count = files.size();
    f.write(reinterpret_cast<const char*>(&count), 4);

    for (auto& path : files) {
        std::ifstream src(path, std::ios::binary);
        if (!src) {
            std::cout << "skip missing: " << path << "\n";
            continue;
        }

        std::vector<uint8_t> data((std::istreambuf_iterator<char>(src)), std::istreambuf_iterator<char>());
        
        std::vector<uint8_t> compressed;
        if (m == M_LZMA) compressed = wasdwasd(data);
        else if (m == M_ZSTD) compressed = stendof2(data);
        else compressed = data;

        uint32_t len = path.size();
        uint64_t o_size = data.size();
        uint64_t c_size = compressed.size();

        f.write(reinterpret_cast<const char*>(&len), 4);
        f.write(path.data(), len);
        f.write(reinterpret_cast<const char*>(&o_size), 8);
        f.write(reinterpret_cast<const char*>(&c_size), 8);
        f.write(reinterpret_cast<const char*>(compressed.data()), c_size);
    }
}

int main(int argc, char** argv) {
    try {
        if (argc < 3) {
            std::cout << "usage: arch pack <out> <file1> [file2...]\n";
            return 1;
        }

        std::string cmd = argv[1];
        if (cmd == "pack") {
            std::string out = argv[2];
            std::vector<std::string> files;
            for (int i = 3; i < argc; ++i) files.push_back(argv[i]);
            
            kakashki(files, out, M_ZSTD);
            std::cout << "done\n";
        }
    } catch (const std::exception& e) {
        std::cerr << "err: " << e.what() << "\n";
        return 1;
    }
}
