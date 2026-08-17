#include "app_server_client.h"

#include <atomic>
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <system_error>

#include <unistd.h>

namespace {

std::atomic<bool> running{true};

void stop(int) {
    running = false;
}

std::filesystem::path ui_state_path() {
    const char* config =
        std::getenv("XDG_CONFIG_HOME");

    if (config != nullptr && *config != '\0') {
        return std::filesystem::path(config) /
            "threaddeck" / "ui-state.json";
    }

    const char* home = std::getenv("HOME");
    return
        home != nullptr && *home != '\0'
            ? std::filesystem::path(home) /
                ".config" / "threaddeck" /
                "ui-state.json"
            : std::filesystem::path(
                ".threaddeck-ui-state.json");
}

std::string remote_shield_host_map() {
    try {
        std::ifstream input(ui_state_path());

        if (!input) {
            return {};
        }

        nlohmann::json state;
        input >> state;
        const auto saved =
            state.value(
                "remoteHostCredentialSaved",
                nlohmann::json::array());
        nlohmann::json hosts =
            nlohmann::json::object();

        if (saved.is_array()) {
            for (const auto& host : saved) {
                if (host.is_string() &&
                    !host.get<std::string>().empty()) {
                    hosts[host.get<std::string>()] =
                        host.get<std::string>();
                }
            }
        }

        return hosts.empty()
            ? std::string{}
            : hosts.dump();
    } catch (const std::exception&) {
        return {};
    }
}

} // namespace

int main() {
    std::signal(SIGINT, stop);
    std::signal(SIGTERM, stop);

    AppServerClient client;
    AppServerClient::ProcessEnvironment environment;
    environment.tablet_accessible = true;

    std::error_code executable_error;
    const auto executable =
        std::filesystem::read_symlink(
            "/proc/self/exe",
            executable_error);

    if (!executable_error && !executable.empty()) {
        const auto executable_directory =
            executable.parent_path();
        environment.shield_enabled = true;
        environment.shield_sudo_directory =
            (executable_directory / "shield-bin").string();
        environment.shield_executor_path =
            (executable_directory /
             "threaddeck-shield-exec").string();
        environment.remote_shield_ssh_directory =
            (executable_directory /
             "remote-shield-bin").string();
        environment.remote_shield_hosts_json =
            remote_shield_host_map();
    }

    std::string error;

    if (!client.start(error, environment)) {
        std::cerr
            << "Could not start the ThreadDeck tablet host: "
            << error
            << '\n';
        return 1;
    }

    const auto initialized =
        client.initialize(
            "threaddeck-tablet-host",
            "ThreadDeck Tablet Host",
            "0.1.0");

    if (!initialized.success) {
        std::cerr
            << "Could not initialize the ThreadDeck tablet host: "
            << initialized.error
            << '\n';
        client.shutdown();
        return 1;
    }

    std::cout
        << "ThreadDeck tablet host is ready on loopback port 4545\n";
    std::cout.flush();

    while (running) {
        ::pause();
    }

    client.shutdown();
    return 0;
}
