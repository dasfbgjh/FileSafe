cmake_minimum_required(VERSION 3.10)
project(FileSafe
    VERSION 1.0.1
    DESCRIPTION "a file crypto tool"
    LANGUAGES CXX
    HOMEPAGE_URL "https://github.com/dasfbgjh/FileSafe"
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/bin)

find_package(Boost REQUIRED CONFIG COMPONENTS
    asio
    process
    filesystem
    regex
    json
)

set(OPENSSL_USE_STATIC_LIBS ON)
find_package(OpenSSL REQUIRED CONFIG COMPONENTS SSL Crypto)

set(RESOURCE_NAME_PREIFX ${PROJECT_NAME})
set(RESOURCE_PATH ${CMAKE_CURRENT_LIST_DIR}/static)
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/resource)

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_definitions(-DRESOURCE_PATH="${RESOURCE_PATH}")
endif()

include(${CMAKE_CURRENT_SOURCE_DIR}/appInfo/AppInfo.cmake)

# =====================================================
add_executable(FileSafe ${WIN_GUI}
    ${APPINFO_SRC}
    src/main.cpp
    src/App.cpp
    src/Utils.cpp
    src/SessionStore.cpp
    src/EncryptQueue.cpp
    src/CryptoFile.cpp
    src/CryptoStore.cpp
    src/http/HttpServer.cpp
)
target_include_directories(FileSafe PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_compile_definitions(FileSafe PRIVATE
    -DCMAKE_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    -DWIN32_LEAN_AND_MEAN
)
target_link_libraries(FileSafe PRIVATE
    ${RESOURCE_NAME_PREIFX}_resource

    Boost::asio
    Boost::process
    Boost::filesystem
    Boost::regex
    Boost::json

    OpenSSL::SSL
    OpenSSL::Crypto
)

if(WIN32)
    target_link_libraries(FileSafe PRIVATE ws2_32 mswsock)
endif()

target_link_options(FileSafe PRIVATE
    -static-libstdc++ -static-libgcc -static
)