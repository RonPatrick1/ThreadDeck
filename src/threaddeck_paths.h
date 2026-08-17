#pragma once

#include <cstdlib>
#include <filesystem>
#include <string>

#include <unistd.h>

namespace threaddeck {

inline std::filesystem::path runtime_directory() {
    const char* xdg_runtime_directory =
        std::getenv("XDG_RUNTIME_DIR");

    if (
        xdg_runtime_directory != nullptr &&
        *xdg_runtime_directory != '\0'
    ) {
        return std::filesystem::path(
            xdg_runtime_directory
        ) / "threaddeck";
    }

    return std::filesystem::path("/tmp") /
        ("threaddeck-" + std::to_string(::getuid()));
}

inline std::filesystem::path app_server_socket_path() {
    return runtime_directory() / "app-server.sock";
}

constexpr int kTabletBridgePort = 4545;

} // namespace threaddeck
