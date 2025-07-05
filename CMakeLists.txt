cmake_minimum_required(VERSION 3.10)
project(makakatool)

set(CMAKE_CXX_STANDARD 17)

# Поиск Zstandard (zstd)
find_package(PkgConfig REQUIRED)
pkg_check_modules(Zstd REQUIRED IMPORTED_TARGET libzstd)

# Поиск LZMA (из xz-utils)
find_package(LibLZMA REQUIRED)

add_executable(makakatool makakatool.cpp)

# Линковка библиотек
target_link_libraries(makakatool 
    PRIVATE 
    PkgConfig::Zstd
    LibLZMA::LibLZMA
)