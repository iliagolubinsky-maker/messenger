cmake_minimum_required(VERSION 3.21)

# =========================
# Project
# =========================
project(server LANGUAGES CXX)

# =========================
# C++ standard
# =========================
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_BUILD_TYPE Debug)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# =========================
# Find packages (system)
# =========================
find_package(nlohmann_json REQUIRED)
find_package(SQLite3 REQUIRED)
set(Boost_NO_BOOST_CMAKE ON)
find_package(Boost REQUIRED COMPONENTS thread)
find_package(OpenSSL REQUIRED)

# libsodium does NOT always provide a CMake config on Linux
find_package(PkgConfig REQUIRED)
pkg_check_modules(SODIUM REQUIRED libsodium)

# =========================
# Executable
# =========================
add_executable(server
    src/server.cpp
)

# =========================
# Include directories
# =========================
target_include_directories(server PRIVATE
    ${SODIUM_INCLUDE_DIRS}
)

# =========================
# Link libraries
# =========================
target_link_libraries(server PRIVATE
    nlohmann_json::nlohmann_json
    SQLite::SQLite3
    Boost::thread
    OpenSSL::SSL
    OpenSSL::Crypto
    ${SODIUM_LIBRARIES}
)

# =========================
# Compile options (Linux)
# =========================
target_compile_options(server PRIVATE
    -Wall
    -Wextra
    -Wpedantic
)

# =========================
# Output directory
# =========================
set_target_properties(server PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
