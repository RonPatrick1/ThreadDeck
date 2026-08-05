cmake_minimum_required(VERSION 3.20)

project(ThreadDeck
    VERSION 0.1.0
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(PkgConfig REQUIRED)
find_package(Threads REQUIRED)

pkg_check_modules(
    GTKMM
    REQUIRED
    IMPORTED_TARGET
    gtkmm-3.0
)

pkg_check_modules(
    NLOHMANN_JSON
    REQUIRED
    IMPORTED_TARGET
    nlohmann_json
)

add_library(codex-app-server-client STATIC
    src/app_server_client.cpp
)

target_include_directories(codex-app-server-client
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(codex-app-server-client
    PUBLIC
        PkgConfig::NLOHMANN_JSON
)

target_compile_options(codex-app-server-client
    PRIVATE
        -Wall
        -Wextra
        -Wpedantic
)

add_executable(threaddeck
    src/main.cpp
)

target_link_libraries(threaddeck
    PRIVATE
        PkgConfig::GTKMM
        codex-app-server-client
        Threads::Threads
)

target_compile_options(threaddeck
    PRIVATE
        -Wall
        -Wextra
        -Wpedantic
)

add_executable(threaddeck-app-server-smoke
    src/app_server_smoke.cpp
)

target_link_libraries(threaddeck-app-server-smoke
    PRIVATE
        codex-app-server-client
)

target_compile_options(threaddeck-app-server-smoke
    PRIVATE
        -Wall
        -Wextra
        -Wpedantic
)
