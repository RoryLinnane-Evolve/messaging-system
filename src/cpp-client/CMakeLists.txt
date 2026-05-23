cmake_minimum_required(VERSION 3.20)
project(securemsg-client CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── System dependencies ───────────────────────────────────────────────────────
find_package(CURL    REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(SODIUM REQUIRED libsodium)

# ── FetchContent dependencies ─────────────────────────────────────────────────
include(FetchContent)

# nlohmann/json — single header
FetchContent_Declare(
    nlohmann_json
    URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.hpp
    DOWNLOAD_NO_EXTRACT TRUE
    DOWNLOAD_DIR ${CMAKE_CURRENT_BINARY_DIR}/include/nlohmann
)
FetchContent_MakeAvailable(nlohmann_json)

# ixwebsocket — WebSocket client with TLS
set(USE_TLS      ON  CACHE BOOL "" FORCE)
set(USE_OPEN_SSL ON  CACHE BOOL "" FORCE)
set(IXWEBSOCKET_INSTALL OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    ixwebsocket
    GIT_REPOSITORY https://github.com/machinezone/IXWebSocket.git
    GIT_TAG        v11.4.5
)
FetchContent_MakeAvailable(ixwebsocket)

# ── Target ───────────────────────────────────────────────────────────────────
add_executable(securemsg
    src/KeyStore.cpp
    src/WebSocketClient.cpp
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
    OpenSSL::SSL
    OpenSSL::Crypto
    ixwebsocket
    ${SODIUM_LIBRARIES}
)

target_compile_options(securemsg PRIVATE ${SODIUM_CFLAGS_OTHER})
