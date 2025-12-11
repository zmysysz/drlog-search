cmake_minimum_required(VERSION 3.16)
project(drlog-search LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(SEVER_NAME_AGENT "drlog-agent")
set(SEVER_NAME_GATEWAY "drlog-gateway")

# Enable coroutine compile flag for non-MSVC compilers if desired
if (MSVC)
  add_compile_options(/await)
else()
  add_compile_options(-fcoroutines)
endif()

# Find Boost components
find_package(Boost 1.74 REQUIRED COMPONENTS system filesystem regex)
find_package(Threads REQUIRED)
find_package(spdlog QUIET)
# zlib is required for gzip indexing (gzopen/gzread/gzclose)
find_package(ZLIB REQUIRED)
# igzip library
link_directories(${CMAKE_SOURCE_DIR}/libs/igzip)

# nlohmann_json: prefer installed, else fetch
find_package(nlohmann_json QUIET)
if (NOT nlohmann_json_FOUND)
  include(FetchContent)
  FetchContent_Declare(json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.2)
  FetchContent_MakeAvailable(json)
endif()

#agent
# Source files
set(SRC_FILES_AGENT
    src/agent/main_agent.cpp
    src/agent/indexer.cpp
    src/agent/agent_handler.cpp
    src/util/config.cpp
    src/util/util.cpp
    src/agent/searcher.cpp
    src/agent/searchers/boolean_searcher.cpp
)

add_executable(${SEVER_NAME_AGENT} ${SRC_FILES_AGENT})

# Include project root and src
target_include_directories(${SEVER_NAME_AGENT} PRIVATE
    ${CMAKE_SOURCE_DIR}/src/gateway
    ${CMAKE_SOURCE_DIR}/src/util
    ${CMAKE_SOURCE_DIR}/src/agent
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}
    usr/local/include
    ${Boost_INCLUDE_DIRS}
)

target_link_libraries(${SEVER_NAME_AGENT} PRIVATE 
    ${Boost_LIBRARIES}
    spdlog::spdlog
    Threads::Threads
    nlohmann_json::nlohmann_json
    libboost_url.so
    ZLIB::ZLIB
    isal.a
)

#gateway
# Source files
set(SRC_FILES_GATEWAY
    src/gateway/main_gateway.cpp
    src/gateway/gateway_handler.cpp
    src/util/config.cpp
    src/util/util.cpp
    src/gateway/agent_manager.cpp
)

add_executable(${SEVER_NAME_GATEWAY} ${SRC_FILES_GATEWAY})

# Include project root and src
target_include_directories(${SEVER_NAME_GATEWAY} PRIVATE
    ${CMAKE_SOURCE_DIR}/src/gateway
    ${CMAKE_SOURCE_DIR}/src/util
    ${CMAKE_SOURCE_DIR}/src/agent
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}
    usr/local/include
    ${Boost_INCLUDE_DIRS}
)

target_link_libraries(${SEVER_NAME_GATEWAY} PRIVATE 
    ${Boost_LIBRARIES}
    spdlog::spdlog
    Threads::Threads
    nlohmann_json::nlohmann_json
    libboost_url.so
    ZLIB::ZLIB
)

# Runtime output
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
