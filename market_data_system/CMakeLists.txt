cmake_minimum_required(VERSION 3.14)
project(market_data_system VERSION 1.0.0 LANGUAGES CXX)

# Modern CMake policy for Boost
cmake_policy(SET CMP0167 NEW)

# C++17 standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Release build optimizations
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -march=native -DNDEBUG")

# Find Boost (header-only Asio doesn't need compiled components on newer Boost)
find_package(Boost 1.70 REQUIRED)

# Fetch fmt library
include(FetchContent)

FetchContent_Declare(
    fmt
    GIT_REPOSITORY https://github.com/fmtlib/fmt.git
    GIT_TAG 10.2.1
)
FetchContent_MakeAvailable(fmt)

# Fetch nlohmann/json
FetchContent_Declare(
    json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
)
FetchContent_MakeAvailable(json)

# Include directories
include_directories(${CMAKE_SOURCE_DIR}/include)

# Common source files
set(COMMON_HEADERS
    include/market_data.hpp
    include/spsc_ring_buffer.hpp
    include/shared_memory.hpp
    include/clock_utils.hpp
)

# Publisher executable
add_executable(publisher
    src/publisher.cpp
    ${COMMON_HEADERS}
)
target_link_libraries(publisher PRIVATE
    Boost::headers
    fmt::fmt
    nlohmann_json::nlohmann_json
    pthread
)

# Shared Memory Consumer executable
add_executable(shm_consumer
    src/shm_consumer.cpp
    ${COMMON_HEADERS}
)
target_link_libraries(shm_consumer PRIVATE
    fmt::fmt
    nlohmann_json::nlohmann_json
    pthread
)

# TCP Consumer executable
add_executable(tcp_consumer
    src/tcp_consumer.cpp
    ${COMMON_HEADERS}
)
target_link_libraries(tcp_consumer PRIVATE
    Boost::headers
    fmt::fmt
    nlohmann_json::nlohmann_json
    pthread
)

# Compiler warnings
target_compile_options(publisher PRIVATE -Wall -Wextra -Wpedantic)
target_compile_options(shm_consumer PRIVATE -Wall -Wextra -Wpedantic)
target_compile_options(tcp_consumer PRIVATE -Wall -Wextra -Wpedantic)
