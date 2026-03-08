cmake_minimum_required(VERSION 3.14)
project(wsocket_io_cpp VERSION 0.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Boost REQUIRED COMPONENTS system)
find_package(nlohmann_json 3.0 REQUIRED)
find_package(CURL REQUIRED)
find_package(Threads REQUIRED)

add_library(wsocket_io
    src/wsocket.cpp
)

target_include_directories(wsocket_io
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(wsocket_io
    PUBLIC
        nlohmann_json::nlohmann_json
    PRIVATE
        Boost::system
        CURL::libcurl
        Threads::Threads
)

install(TARGETS wsocket_io LIBRARY DESTINATION lib)
install(DIRECTORY include/wsocket DESTINATION include)
