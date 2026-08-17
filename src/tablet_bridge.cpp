#include "threaddeck_paths.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <set>
#include <stdexcept>
#include <string>
#include <thread>

#include <nlohmann/json.hpp>

#include <arpa/inet.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace {

std::atomic<bool> running{true};
int listener_fd = -1;
std::mutex ui_state_mutex;

void handle_signal(int) {
    running = false;

    if (listener_fd >= 0) {
        ::close(listener_fd);
        listener_fd = -1;
    }
}

bool write_all(
    int fd,
    const char* data,
    std::size_t size
) {
    std::size_t written = 0;

    while (written < size) {
        const ssize_t result =
            ::send(
                fd,
                data + written,
                size - written,
                MSG_NOSIGNAL);

        if (result > 0) {
            written += static_cast<std::size_t>(result);
            continue;
        }

        if (result < 0 && errno == EINTR) {
            continue;
        }

        return false;
    }

    return true;
}

std::filesystem::path ui_state_path() {
    const char* xdg_config_home =
        std::getenv("XDG_CONFIG_HOME");

    if (
        xdg_config_home != nullptr &&
        *xdg_config_home != '\0'
    ) {
        return std::filesystem::path(
            xdg_config_home
        ) / "threaddeck" / "ui-state.json";
    }

    const char* home = std::getenv("HOME");

    if (home != nullptr && *home != '\0') {
        return std::filesystem::path(home) /
            ".config" / "threaddeck" /
            "ui-state.json";
    }

    return ".threaddeck-ui-state.json";
}

bool read_http_headers(
    int fd,
    std::string& headers
) {
    constexpr std::size_t maximum_size = 64 * 1024;
    char buffer[4096];

    while (
        headers.find("\r\n\r\n") ==
            std::string::npos &&
        headers.size() < maximum_size
    ) {
        const ssize_t result =
            ::recv(fd, buffer, sizeof(buffer), 0);

        if (result > 0) {
            headers.append(
                buffer,
                static_cast<std::size_t>(result));
            continue;
        }

        if (result < 0 && errno == EINTR) {
            continue;
        }

        return false;
    }

    return
        headers.find("\r\n\r\n") !=
        std::string::npos;
}

bool is_ui_state_request(const std::string& headers) {
    constexpr char route[] =
        "GET /threaddeck/ui-state HTTP/1.1\r\n";

    return headers.rfind(route, 0) == 0;
}

bool is_ui_state_update_request(
    const std::string& headers
) {
    constexpr char route[] =
        "POST /threaddeck/ui-state HTTP/1.1\r\n";

    return headers.rfind(route, 0) == 0;
}

std::size_t request_content_length(
    const std::string& headers
) {
    constexpr char name[] = "content-length:";
    std::size_t line_start = 0;

    while (line_start < headers.size()) {
        const std::size_t line_end =
            headers.find("\r\n", line_start);

        if (line_end == std::string::npos) {
            break;
        }

        std::string line =
            headers.substr(
                line_start,
                line_end - line_start);

        std::transform(
            line.begin(),
            line.end(),
            line.begin(),
            [](unsigned char value) {
                return static_cast<char>(
                    std::tolower(value));
            });

        if (line.rfind(name, 0) == 0) {
            try {
                return static_cast<std::size_t>(
                    std::stoull(
                        line.substr(sizeof(name) - 1)));
            } catch (...) {
                return 0;
            }
        }

        line_start = line_end + 2;
    }

    return 0;
}

bool read_http_body(
    int fd,
    std::string& request,
    std::string& body,
    std::size_t maximum_body_size =
        1024 * 1024
) {
    const std::size_t header_end =
        request.find("\r\n\r\n");

    if (header_end == std::string::npos) {
        return false;
    }

    const std::size_t content_length =
        request_content_length(request);

    if (
        content_length == 0 ||
        content_length > maximum_body_size
    ) {
        return false;
    }

    const std::size_t body_start = header_end + 4;
    char buffer[4096];

    while (
        request.size() <
        body_start + content_length
    ) {
        const ssize_t result =
            ::recv(fd, buffer, sizeof(buffer), 0);

        if (result > 0) {
            request.append(
                buffer,
                static_cast<std::size_t>(result));
            continue;
        }

        if (result < 0 && errno == EINTR) {
            continue;
        }

        return false;
    }

    body = request.substr(
        body_start,
        content_length);
    return true;
}

bool is_attachment_upload_request(
    const std::string& headers,
    std::string& kind
) {
    constexpr char image_route[] =
        "POST /threaddeck/attachment/image HTTP/1.1\r\n";
    constexpr char audio_route[] =
        "POST /threaddeck/attachment/audio HTTP/1.1\r\n";

    if (headers.rfind(image_route, 0) == 0) {
        kind = "image";
        return true;
    }

    if (headers.rfind(audio_route, 0) == 0) {
        kind = "audio";
        return true;
    }

    return false;
}

std::string request_header_value(
    const std::string& headers,
    const std::string& requested_name
) {
    std::string name = requested_name;
    std::transform(
        name.begin(),
        name.end(),
        name.begin(),
        [](unsigned char value) {
            return static_cast<char>(
                std::tolower(value));
        });
    name += ':';

    std::size_t line_start = 0;

    while (line_start < headers.size()) {
        const std::size_t line_end =
            headers.find("\r\n", line_start);

        if (line_end == std::string::npos) {
            break;
        }

        const std::string line =
            headers.substr(
                line_start,
                line_end - line_start);
        std::string lower_line = line;
        std::transform(
            lower_line.begin(),
            lower_line.end(),
            lower_line.begin(),
            [](unsigned char value) {
                return static_cast<char>(
                    std::tolower(value));
            });

        if (lower_line.rfind(name, 0) == 0) {
            const std::size_t value_start =
                line.find_first_not_of(
                    " \t",
                    name.size());
            return value_start == std::string::npos
                ? std::string{}
                : line.substr(value_start);
        }

        line_start = line_end + 2;
    }

    return {};
}

nlohmann::json filtered_ui_state() {
    std::ifstream input(ui_state_path());

    if (!input) {
        return {
            {"version", 1},
            {"projectFolders", nlohmann::json::array()},
            {"projectPaths", nlohmann::json::object()},
            {"folderLabels", nlohmann::json::object()},
            {"threadLabels", nlohmann::json::object()},
            {
                "threadProjectAssignments",
                nlohmann::json::object(),
            },
            {"projectThreadSorts", nlohmann::json::object()},
            {"projectSort", "updated-desc"},
            {"selectedFolder", ""},
            {"selectedProjectId", ""},
            {"collapsedProjectFolders", nlohmann::json::array()},
            {"threadAccessSelections", nlohmann::json::object()},
            {"threadShieldSelections", nlohmann::json::array()},
            {"threadAutoCopySelections", nlohmann::json::array()},
            {"threadRemoteShieldHosts", nlohmann::json::object()},
            {"threadObservedRemoteHosts", nlohmann::json::object()},
            {"remoteHostLabels", nlohmann::json::object()},
            {"remoteHostCredentialSaved", nlohmann::json::array()},
            {"pausedThreads", nlohmann::json::array()},
            {"threadConfiguredApprovalPolicies", nlohmann::json::object()},
            {"threadConfiguredSandboxPolicies", nlohmann::json::object()},
            {"threadModelSelections", nlohmann::json::object()},
            {"threadReasoningSelections", nlohmann::json::object()},
            {"theme", "system"},
            {"sidebarVisible", true},
            {"contextPanelVisible", true},
            {"remoteHostsPanelVisible", false},
        };
    }

    nlohmann::json state;
    input >> state;

    nlohmann::json filtered = {
        {"version", 1},
    };

    const std::pair<const char*, nlohmann::json>
        fields[] = {
            {"projectFolders", nlohmann::json::array()},
            {"projectPaths", nlohmann::json::object()},
            {"folderLabels", nlohmann::json::object()},
            {"threadLabels", nlohmann::json::object()},
            {
                "threadProjectAssignments",
                nlohmann::json::object(),
            },
            {"projectThreadSorts", nlohmann::json::object()},
            {"projectSort", "updated-desc"},
            {"selectedFolder", ""},
            {"selectedProjectId", ""},
            {"collapsedProjectFolders", nlohmann::json::array()},
            {"threadAccessSelections", nlohmann::json::object()},
            {"threadShieldSelections", nlohmann::json::array()},
            {"threadAutoCopySelections", nlohmann::json::array()},
            {"threadRemoteShieldHosts", nlohmann::json::object()},
            {"threadObservedRemoteHosts", nlohmann::json::object()},
            {"remoteHostLabels", nlohmann::json::object()},
            {"remoteHostCredentialSaved", nlohmann::json::array()},
            {"pausedThreads", nlohmann::json::array()},
            {"threadConfiguredApprovalPolicies", nlohmann::json::object()},
            {"threadConfiguredSandboxPolicies", nlohmann::json::object()},
            {"threadModelSelections", nlohmann::json::object()},
            {"threadReasoningSelections", nlohmann::json::object()},
            {"theme", "system"},
            {"sidebarVisible", true},
            {"contextPanelVisible", true},
            {"remoteHostsPanelVisible", false},
        };

    for (const auto& field : fields) {
        filtered[field.first] =
            state.contains(field.first)
                ? state[field.first]
                : field.second;
    }

    return filtered;
}

void serve_ui_state(int client_fd) {
    std::string body;

    try {
        std::lock_guard<std::mutex> lock(
            ui_state_mutex);
        body = filtered_ui_state().dump();
    } catch (const std::exception& error) {
        body = nlohmann::json{
            {"error", error.what()},
        }.dump();
    }

    const std::string response =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: application/json; charset=utf-8\r\n"
        "Cache-Control: no-store\r\n"
        "Connection: close\r\n"
        "Content-Length: " +
        std::to_string(body.size()) +
        "\r\n\r\n" + body;

    write_all(client_fd, response.data(), response.size());
}

void serve_json_response(
    int client_fd,
    int status,
    const char* status_text,
    const nlohmann::json& value
) {
    const std::string body = value.dump();
    const std::string response =
        "HTTP/1.1 " +
        std::to_string(status) +
        " " + status_text + "\r\n"
        "Content-Type: application/json; charset=utf-8\r\n"
        "Cache-Control: no-store\r\n"
        "Connection: close\r\n"
        "Content-Length: " +
        std::to_string(body.size()) +
        "\r\n\r\n" + body;

    write_all(
        client_fd,
        response.data(),
        response.size());
}

void serve_ui_state_update(
    int client_fd,
    std::string& request
) {
    std::string body;

    if (!read_http_body(client_fd, request, body)) {
        serve_json_response(
            client_fd,
            400,
            "Bad Request",
            {{"error", "A JSON request body is required"}});
        return;
    }

    try {
        const auto requested =
            nlohmann::json::parse(body);
        const auto updates =
            requested.contains("updates")
                ? requested["updates"]
                : nlohmann::json{};

        if (!updates.is_object()) {
            throw std::runtime_error(
                "updates must be a JSON object");
        }

        static const std::set<std::string>
            allowed_fields = {
                "selectedFolder",
                "selectedProjectId",
                "projectSort",
                "projectThreadSorts",
                "projectFolders",
                "projectPaths",
                "collapsedProjectFolders",
                "folderLabels",
                "threadLabels",
                "threadAccessSelections",
                "threadModelSelections",
                "threadReasoningSelections",
                "threadShieldSelections",
                "threadAutoCopySelections",
                "remoteHostLabels",
                "remoteHostCredentialSaved",
                "threadRemoteShieldHosts",
                "threadObservedRemoteHosts",
                "pausedThreads",
                "threadProjectAssignments",
                "movedThreadSummaries",
                "threadConfiguredApprovalPolicies",
                "threadConfiguredSandboxPolicies",
                "theme",
                "sidebarVisible",
                "contextPanelVisible",
                "remoteHostsPanelVisible",
            };

        std::lock_guard<std::mutex> lock(
            ui_state_mutex);
        const auto state_path = ui_state_path();
        nlohmann::json state =
            nlohmann::json::object();
        std::ifstream input(state_path);

        if (input) {
            input >> state;
        }

        if (!state.is_object()) {
            throw std::runtime_error(
                "ThreadDeck UI state is not a JSON object");
        }

        for (auto item = updates.begin();
             item != updates.end();
             ++item) {
            if (allowed_fields.find(item.key()) ==
                allowed_fields.end()) {
                throw std::runtime_error(
                    "Unsupported ThreadDeck setting: " +
                    item.key());
            }

            state[item.key()] = item.value();
        }

        std::filesystem::create_directories(
            state_path.parent_path());
        const auto temporary_path =
            std::filesystem::path(
                state_path.string() +
                ".tablet.tmp");

        {
            std::ofstream output(
                temporary_path,
                std::ios::trunc);

            if (!output) {
                throw std::runtime_error(
                    "Could not open the temporary UI state file");
            }

            output << state.dump(2) << '\n';

            if (!output) {
                throw std::runtime_error(
                    "Could not write the temporary UI state file");
            }
        }

        std::filesystem::rename(
            temporary_path,
            state_path);

        serve_json_response(
            client_fd,
            200,
            "OK",
            {{"ok", true}});
    } catch (const std::exception& error) {
        serve_json_response(
            client_fd,
            400,
            "Bad Request",
            {{"error", error.what()}});
    }
}

void serve_attachment_upload(
    int client_fd,
    std::string& request,
    const std::string& kind
) {
    std::string body;

    if (!read_http_body(
            client_fd,
            request,
            body,
            64 * 1024 * 1024)) {
        serve_json_response(
            client_fd,
            400,
            "Bad Request",
            {{"error", "The attachment is empty or larger than 64 MB"}});
        return;
    }

    try {
        std::string extension =
            request_header_value(
                request,
                "X-ThreadDeck-Extension");
        extension.erase(
            std::remove_if(
                extension.begin(),
                extension.end(),
                [](unsigned char value) {
                    return
                        !std::isalnum(value) &&
                        value != '.';
                }),
            extension.end());

        if (
            extension.empty() ||
            extension.size() > 12
        ) {
            extension = ".bin";
        } else if (extension.front() != '.') {
            extension.insert(extension.begin(), '.');
        }

        const auto directory =
            threaddeck::runtime_directory() /
            "tablet-attachments";
        std::filesystem::create_directories(
            directory);
        const auto timestamp =
            std::chrono::steady_clock::now()
                .time_since_epoch()
                .count();
        const auto path =
            directory /
            (
                kind + "-" +
                std::to_string(timestamp) +
                extension
            );
        std::ofstream output(
            path,
            std::ios::binary |
            std::ios::trunc);

        if (!output) {
            throw std::runtime_error(
                "Could not create the host attachment file");
        }

        output.write(
            body.data(),
            static_cast<std::streamsize>(
                body.size()));

        if (!output) {
            throw std::runtime_error(
                "Could not write the host attachment file");
        }

        serve_json_response(
            client_fd,
            200,
            "OK",
            {
                {"path", path.string()},
                {"size", body.size()},
                {"kind", kind},
            });
    } catch (const std::exception& error) {
        serve_json_response(
            client_fd,
            500,
            "Internal Server Error",
            {{"error", error.what()}});
    }
}

int connect_to_app_server(
    const std::filesystem::path& socket_path
) {
    const std::string path = socket_path.string();

    if (path.size() >= sizeof(sockaddr_un::sun_path)) {
        return -1;
    }

    const int fd =
        ::socket(AF_UNIX, SOCK_STREAM, 0);

    if (fd < 0) {
        return -1;
    }

    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    std::memcpy(
        address.sun_path,
        path.c_str(),
        path.size() + 1);

    if (
        ::connect(
            fd,
            reinterpret_cast<sockaddr*>(&address),
            sizeof(address)) != 0
    ) {
        ::close(fd);
        return -1;
    }

    return fd;
}

void relay_client(
    int client_fd,
    std::filesystem::path socket_path
) {
    std::string client_headers;

    if (!read_http_headers(client_fd, client_headers)) {
        ::close(client_fd);
        return;
    }

    if (is_ui_state_request(client_headers)) {
        serve_ui_state(client_fd);
        ::close(client_fd);
        return;
    }

    if (is_ui_state_update_request(client_headers)) {
        serve_ui_state_update(
            client_fd,
            client_headers);
        ::close(client_fd);
        return;
    }

    std::string attachment_kind;
    if (is_attachment_upload_request(
            client_headers,
            attachment_kind)) {
        serve_attachment_upload(
            client_fd,
            client_headers,
            attachment_kind);
        ::close(client_fd);
        return;
    }

    const int server_fd =
        connect_to_app_server(socket_path);

    if (server_fd < 0) {
        constexpr char unavailable[] =
            "HTTP/1.1 503 Service Unavailable\r\n"
            "Connection: close\r\n"
            "Content-Length: 0\r\n\r\n";

        write_all(
            client_fd,
            unavailable,
            sizeof(unavailable) - 1);
        ::close(client_fd);
        return;
    }

    if (
        !write_all(
            server_fd,
            client_headers.data(),
            client_headers.size())
    ) {
        ::close(server_fd);
        ::close(client_fd);
        return;
    }

    pollfd descriptors[2]{
        {
            client_fd,
            static_cast<short>(POLLIN | POLLHUP),
            0,
        },
        {
            server_fd,
            static_cast<short>(POLLIN | POLLHUP),
            0,
        },
    };

    char buffer[16384];

    while (running) {
        const int result =
            ::poll(descriptors, 2, 1000);

        if (result < 0) {
            if (errno == EINTR) {
                continue;
            }

            break;
        }

        if (result == 0) {
            continue;
        }

        for (int index = 0; index < 2; ++index) {
            if (
                (descriptors[index].revents &
                 (POLLIN | POLLHUP | POLLERR)) == 0
            ) {
                continue;
            }

            const int source_fd =
                descriptors[index].fd;
            const int destination_fd =
                descriptors[1 - index].fd;

            const ssize_t read_result =
                ::recv(
                    source_fd,
                    buffer,
                    sizeof(buffer),
                    0);

            if (read_result <= 0) {
                ::shutdown(destination_fd, SHUT_WR);
                goto finished;
            }

            if (
                !write_all(
                    destination_fd,
                    buffer,
                    static_cast<std::size_t>(read_result))
            ) {
                goto finished;
            }
        }
    }

finished:
    ::close(server_fd);
    ::close(client_fd);
}

int parse_port(const char* value) {
    try {
        const int port = std::stoi(value);

        if (port > 0 && port <= 65535) {
            return port;
        }
    } catch (...) {
    }

    return -1;
}

} // namespace

int main(int argc, char* argv[]) {
    std::filesystem::path socket_path =
        threaddeck::app_server_socket_path();
    int port = threaddeck::kTabletBridgePort;

    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];

        if (argument == "--socket" && index + 1 < argc) {
            socket_path = argv[++index];
            continue;
        }

        if (argument == "--port" && index + 1 < argc) {
            port = parse_port(argv[++index]);

            if (port < 0) {
                std::cerr << "Invalid tablet bridge port\n";
                return 2;
            }

            continue;
        }

        if (argument == "--help") {
            std::cout
                << "Usage: threaddeck-tablet-bridge "
                << "[--socket PATH] [--port PORT]\n";
            return 0;
        }

        std::cerr
            << "Unknown argument: "
            << argument
            << '\n';
        return 2;
    }

    std::signal(SIGINT, handle_signal);
    std::signal(SIGTERM, handle_signal);

    listener_fd =
        ::socket(AF_INET, SOCK_STREAM, 0);

    if (listener_fd < 0) {
        std::cerr
            << "Could not create tablet bridge socket: "
            << std::strerror(errno)
            << '\n';
        return 1;
    }

    const int reuse_address = 1;
    ::setsockopt(
        listener_fd,
        SOL_SOCKET,
        SO_REUSEADDR,
        &reuse_address,
        sizeof(reuse_address));

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(
        static_cast<std::uint16_t>(port));
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    if (
        ::bind(
            listener_fd,
            reinterpret_cast<sockaddr*>(&address),
            sizeof(address)) != 0
    ) {
        std::cerr
            << "Could not bind tablet bridge to 127.0.0.1:"
            << port
            << ": "
            << std::strerror(errno)
            << '\n';
        ::close(listener_fd);
        listener_fd = -1;
        return 1;
    }

    if (::listen(listener_fd, 8) != 0) {
        std::cerr
            << "Could not listen for tablet connections: "
            << std::strerror(errno)
            << '\n';
        ::close(listener_fd);
        listener_fd = -1;
        return 1;
    }

    std::cout
        << "ThreadDeck tablet bridge listening on 127.0.0.1:"
        << port
        << " for "
        << socket_path
        << '\n';

    while (running) {
        const int client_fd =
            ::accept(listener_fd, nullptr, nullptr);

        if (client_fd < 0) {
            if (errno == EINTR) {
                continue;
            }

            if (!running) {
                break;
            }

            std::cerr
                << "Could not accept tablet connection: "
                << std::strerror(errno)
                << '\n';
            continue;
        }

        std::thread(
            relay_client,
            client_fd,
            socket_path
        ).detach();
    }

    if (listener_fd >= 0) {
        ::close(listener_fd);
        listener_fd = -1;
    }

    return 0;
}
