cmake_minimum_required(VERSION 3.20)

project(ThreadDeck
    VERSION 0.1.0
    LANGUAGES CXX
)

include(GNUInstallDirs)

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

pkg_check_modules(
    LIBSECRET
    REQUIRED
    IMPORTED_TARGET
    libsecret-1
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
    src/context_panel.cpp
    src/secret_store.cpp
    src/thread_header.cpp
)

add_executable(threaddeck-shield-control
    src/shield_control.cpp
)

add_executable(threaddeck-tablet-bridge
    src/tablet_bridge.cpp
)

target_link_libraries(threaddeck-tablet-bridge
    PRIVATE
        PkgConfig::NLOHMANN_JSON
        Threads::Threads
)

target_compile_options(threaddeck-tablet-bridge
    PRIVATE
        -Wall
        -Wextra
        -Wpedantic
)

add_executable(threaddeck-tablet-host
    src/tablet_host.cpp
)

target_link_libraries(threaddeck-tablet-host
    PRIVATE
        codex-app-server-client
)

target_compile_options(threaddeck-tablet-host
    PRIVATE
        -Wall
        -Wextra
        -Wpedantic
)

target_compile_options(threaddeck-shield-control
    PRIVATE
        -Wall
        -Wextra
        -Wpedantic
)

add_executable(threaddeck-shield-exec
    src/shield_exec.cpp
)

target_compile_options(threaddeck-shield-exec
    PRIVATE
        -Wall
        -Wextra
        -Wpedantic
)

add_executable(threaddeck-shield-sudo
    src/shield_sudo.cpp
)

set_target_properties(threaddeck-shield-sudo
    PROPERTIES
        OUTPUT_NAME sudo
        RUNTIME_OUTPUT_DIRECTORY
            ${CMAKE_BINARY_DIR}/shield-bin
)

target_compile_options(threaddeck-shield-sudo
    PRIVATE
        -Wall
        -Wextra
        -Wpedantic
)

add_executable(threaddeck-remote-shield-ssh
    src/remote_shield_ssh.cpp
    src/secret_store.cpp
)

set_target_properties(threaddeck-remote-shield-ssh
    PROPERTIES
        OUTPUT_NAME ssh
        RUNTIME_OUTPUT_DIRECTORY
            ${CMAKE_BINARY_DIR}/remote-shield-bin
)

target_link_libraries(threaddeck-remote-shield-ssh
    PRIVATE
        PkgConfig::GTKMM
        PkgConfig::LIBSECRET
        PkgConfig::NLOHMANN_JSON
)

target_compile_options(threaddeck-remote-shield-ssh
    PRIVATE
        -Wall
        -Wextra
        -Wpedantic
)

target_link_libraries(threaddeck
    PRIVATE
        PkgConfig::GTKMM
        PkgConfig::LIBSECRET
        codex-app-server-client
        Threads::Threads
)

target_compile_options(threaddeck
    PRIVATE
        -Wall
        -Wextra
        -Wpedantic
)

set(THREADDECK_COMPLETION_SOUND
    ${CMAKE_CURRENT_SOURCE_DIR}/assets/sounds/task-complete.flac
)

add_custom_command(
    TARGET threaddeck
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
        ${CMAKE_BINARY_DIR}/assets/sounds
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${THREADDECK_COMPLETION_SOUND}
        ${CMAKE_BINARY_DIR}/assets/sounds/task-complete.flac
    VERBATIM
)

install(
    FILES ${THREADDECK_COMPLETION_SOUND}
    DESTINATION ${CMAKE_INSTALL_DATADIR}/threaddeck/sounds
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
