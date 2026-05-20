cmake_minimum_required(VERSION 3.20)
project(securemsg-client CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Dependencies
find_package(CURL REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(SODIUM REQUIRED libsodium)

include(FetchContent)
FetchContent_Declare(
    nlohmann_json
    URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.hpp
    DOWNLOAD_NO_EXTRACT TRUE
    DOWNLOAD_DIR ${CMAKE_CURRENT_BINARY_DIR}/include/nlohmann
)
FetchContent_MakeAvailable(nlohmann_json)

add_executable(securemsg
    src/Client.cpp
    src/main.cpp
)

target_include_directories(securemsg PRIVATE
    include
    ${CMAKE_CURRENT_BINARY_DIR}/include
    ${SODIUM_INCLUDE_DIRS}
)

target_link_libraries(securemsg PRIVATE
    CURL::libcurl
    ${SODIUM_LIBRARIES}
)

target_compile_options(securemsg PRIVATE ${SODIUM_CFLAGS_OTHER})
