cmake_minimum_required(VERSION 3.16)
project(messenger_client LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Find Boost WITH thread component - you definitely need this!
find_package(Boost REQUIRED COMPONENTS thread)

# Find OpenSSL
find_package(OpenSSL REQUIRED)


# Find libsodium
find_package(PkgConfig REQUIRED)
pkg_check_modules(SODIUM REQUIRED libsodium)

# Create the library with correct paths
add_library(messenger_client SHARED
    client.cpp
    client.hpp
    messenger_client_api.cpp
)

# Include directories
target_include_directories(messenger_client PRIVATE
    ${SODIUM_INCLUDE_DIRS}
    ${Boost_INCLUDE_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# Link libraries - MUST include Boost::thread!
target_link_libraries(messenger_client PRIVATE
    Boost::thread
    OpenSSL::SSL
    OpenSSL::Crypto
    ${SODIUM_LIBRARIES}
    pthread
)

# Set output properties
set_target_properties(messenger_client PROPERTIES
    OUTPUT_NAME "messenger_client"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)

