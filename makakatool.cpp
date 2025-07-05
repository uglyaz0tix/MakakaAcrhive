#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <filesystem>
#include <lzma.h>
#include <zstd.h>
#include <cstring>

namespace fs = std::filesystem;

constexpr uint32_t MAKAKA_SIGNATURE = 0x4D4B4B41;
constexpr uint16_t MAKAKA_VERSION = 0x0100;

enum CompressionType {
    COMPRESS_NONE = 0,
    COMPRESS_LZMA = 1,
    COMPRESS_ZSTD = 2
};

std::vector<uint8_t> compressWithLZMA(const std::vector<uint8_t>& input) {
    lzma_stream stream = LZMA_STREAM_INIT;
    if (lzma_easy_encoder(&stream, 9 | LZMA_PRESET_EXTREME, LZMA_CHECK_CRC64) != LZMA_OK) {
        throw std::runtime_error("LZMA compression initialization failed");
    }

    stream.next_in = input.data();
    stream.avail_in = input.size();

    std::vector<uint8_t> output(input.size() * 1.5);
    stream.next_out = output.data();
    stream.avail_out = output.size();

    lzma_code(&stream, LZMA_FINISH);
    output.resize(stream.total_out);
    lzma_end(&stream);

    return output;
}

std::vector<uint8_t> compressWithZSTD(const std::vector<uint8_t>& input) {
    size_t max_size = ZSTD_compressBound(input.size());
    std::vector<uint8_t> output(max_size);

    size_t compressed_size = ZSTD_compress(
        output.data(), output.size(),
        input.data(), input.size(),
        ZSTD_maxCLevel()
    );

    if (ZSTD_isError(compressed_size)) {
        throw std::runtime_error("ZSTD compression failed: " + std::string(ZSTD_getErrorName(compressed_size)));
    }

    output.resize(compressed_size);
    return output;
}

std::vector<uint8_t> decompressZSTD(const std::vector<uint8_t>& input, size_t original_size) {
    std::vector<uint8_t> output(original_size);
    size_t result = ZSTD_decompress(
        output.data(), output.size(),
        input.data(), input.size()
    );

    if (ZSTD_isError(result)) {
        throw std::runtime_error("ZSTD decompression failed: " + std::string(ZSTD_getErrorName(result)));
    }

    return output;
}

void createArchive(const std::vector<std::string>& files, const std::string& output_path, CompressionType compression) {
    std::ofstream out(output_path, std::ios::binary);
    if (!out) throw std::runtime_error("Failed to create output file");

    out.write(reinterpret_cast<const char*>(&MAKAKA_SIGNATURE), 4);
    out.write(reinterpret_cast<const char*>(&MAKAKA_VERSION), 2);
    out.write(reinterpret_cast<const char*>(&compression), 2);

    uint32_t file_count = files.size();
    out.write(reinterpret_cast<const char*>(&file_count), 4);

    for (const auto& file_path : files) {
        std::ifstream in(file_path, std::ios::binary);
        if (!in) {
            std::cerr << "Warning: Skipping missing file " << file_path << std::endl;
            continue;
        }

        std::vector<uint8_t> file_data(
            (std::istreambuf_iterator<char>(in)),
            std::istreambuf_iterator<char>()
        );
        in.close();

        std::vector<uint8_t> compressed_data;
        switch (compression) {
            case COMPRESS_LZMA:
                compressed_data = compressWithLZMA(file_data);
                break;
            case COMPRESS_ZSTD:
                compressed_data = compressWithZSTD(file_data);
                break;
            default:
                compressed_data = file_data;
        }

        uint32_t name_length = file_path.size();
        uint64_t original_size = file_data.size();
        uint64_t compressed_size = compressed_data.size();

        out.write(reinterpret_cast<const char*>(&name_length), 4);
        out.write(file_path.c_str(), name_length);
        out.write(reinterpret_cast<const char*>(&original_size), 8);
        out.write(reinterpret_cast<const char*>(&compressed_size), 8);
        out.write(reinterpret_cast<const char*>(compressed_data.data()), compressed_size);
    }
}

void extractArchive(const std::string& archive_path, const std::string& output_dir, bool verbose = false) {
    std::ifstream in(archive_path, std::ios::binary);
    if (!in) throw std::runtime_error("Failed to open archive");

    uint32_t signature;
    in.read(reinterpret_cast<char*>(&signature), 4);
    if (signature != MAKAKA_SIGNATURE) {
        throw std::runtime_error("Invalid file format");
    }

    uint16_t version, compression;
    in.read(reinterpret_cast<char*>(&version), 2);
    in.read(reinterpret_cast<char*>(&compression), 2);

    if (verbose) {
        std::cout << "Archive version: " << (version >> 8) << "." << (version & 0xFF) << "\n";
        std::cout << "Compression: ";
        switch (compression) {
            case COMPRESS_LZMA: std::cout << "LZMA\n"; break;
            case COMPRESS_ZSTD: std::cout << "ZSTD\n"; break;
            default: std::cout << "None\n";
        }
    }

    uint32_t file_count;
    in.read(reinterpret_cast<char*>(&file_count), 4);
    if (verbose) std::cout << "Files in archive: " << file_count << "\n";

    for (uint32_t i = 0; i < file_count; ++i) {
        uint32_t name_length;
        in.read(reinterpret_cast<char*>(&name_length), 4);

        std::string file_name(name_length, '\0');
        in.read(&file_name[0], name_length);

        uint64_t original_size, compressed_size;
        in.read(reinterpret_cast<char*>(&original_size), 8);
        in.read(reinterpret_cast<char*>(&compressed_size), 8);

        if (verbose) {
            std::cout << "Extracting " << file_name << " (" 
                      << original_size << " -> " << compressed_size << " bytes)\n";
        }

        std::vector<uint8_t> compressed_data(compressed_size);
        in.read(reinterpret_cast<char*>(compressed_data.data()), compressed_size);

        std::vector<uint8_t> file_data;
        if (compression == COMPRESS_ZSTD) {
            file_data = decompressZSTD(compressed_data, original_size);
        } else {
            file_data = compressed_data;
        }

        fs::path full_path = fs::path(output_dir) / file_name;
        fs::create_directories(full_path.parent_path());

        std::ofstream out(full_path, std::ios::binary);
        out.write(reinterpret_cast<const char*>(file_data.data()), file_data.size());
    }
}

void listArchiveContents(const std::string& archive_path) {
    std::ifstream in(archive_path, std::ios::binary);
    if (!in) throw std::runtime_error("Failed to open archive");

    uint32_t signature;
    in.read(reinterpret_cast<char*>(&signature), 4);
    if (signature != MAKAKA_SIGNATURE) {
        throw std::runtime_error("Invalid file format");
    }

    uint16_t version, compression;
    in.read(reinterpret_cast<char*>(&version), 2);
    in.read(reinterpret_cast<char*>(&compression), 2);

    std::cout << "Archive: " << archive_path << "\n";
    std::cout << "Version: " << (version >> 8) << "." << (version & 0xFF) << "\n";
    std::cout << "Compression: ";
    switch (compression) {
        case COMPRESS_LZMA: std::cout << "LZMA\n"; break;
        case COMPRESS_ZSTD: std::cout << "ZSTD\n"; break;
        default: std::cout << "None\n";
    }

    uint32_t file_count;
    in.read(reinterpret_cast<char*>(&file_count), 4);
    std::cout << "Files: " << file_count << "\n\n";

    for (uint32_t i = 0; i < file_count; ++i) {
        uint32_t name_length;
        in.read(reinterpret_cast<char*>(&name_length), 4);

        std::string file_name(name_length, '\0');
        in.read(&file_name[0], name_length);

        uint64_t original_size, compressed_size;
        in.read(reinterpret_cast<char*>(&original_size), 8);
        in.read(reinterpret_cast<char*>(&compressed_size), 8);

        std::cout << file_name << " (" << original_size << " bytes, compressed to " 
                  << compressed_size << " bytes)\n";

        in.seekg(compressed_size, std::ios::cur);
    }
}

struct ProgramOptions {
    std::string command;
    std::vector<std::string> files;
    std::string output_path;
    CompressionType compression = COMPRESS_ZSTD;
    bool verbose = false;
};

ProgramOptions parseArguments(int argc, char* argv[]) {
    ProgramOptions options;
    if (argc < 2) {
        throw std::runtime_error(
            "Usage:\n"
            "  pack <files...> -o <output.makaka> [-c lzma|zstd]\n"
            "  unpack <archive.makaka> [-o output_dir] [-v]\n"
            "  list <archive.makaka>"
        );
    }

    options.command = argv[1];
    for (int i = 2; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-o" && i + 1 < argc) {
            options.output_path = argv[++i];
        } else if (arg == "-c" && i + 1 < argc) {
            std::string method = argv[++i];
            if (method == "lzma") options.compression = COMPRESS_LZMA;
            else if (method == "zstd") options.compression = COMPRESS_ZSTD;
            else throw std::runtime_error("Unknown compression method");
        } else if (arg == "-v") {
            options.verbose = true;
        } else if (arg[0] != '-') {
            options.files.push_back(arg);
        }
    }
    return options;
}

int main(int argc, char* argv[]) {
    try {
        ProgramOptions options = parseArguments(argc, argv);

        if (options.command == "pack") {
            if (options.files.empty()) throw std::runtime_error("No input files specified");
            std::string output = options.output_path.empty() ? "archive.makaka" : options.output_path;
            createArchive(options.files, output, options.compression);
            std::cout << "Created archive: " << output << std::endl;
        } 
        else if (options.command == "unpack") {
            if (options.files.empty()) throw std::runtime_error("No archive specified");
            std::string output_dir = options.output_path.empty() ? "." : options.output_path;
            extractArchive(options.files[0], output_dir, options.verbose);
            std::cout << "Extracted to: " << output_dir << std::endl;
        } 
        else if (options.command == "list") {
            if (options.files.empty()) throw std::runtime_error("No archive specified");
            listArchiveContents(options.files[0]);
        } 
        else {
            throw std::runtime_error("Unknown command: " + options.command);
        }
    } 
    catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}