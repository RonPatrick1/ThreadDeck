#include "app_server_client.h"
#include "threaddeck_paths.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <random>
#include <string>

#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {

constexpr const char* kThreadDeckFormattingContext =
    "When you provide a shell command that the user should copy and run "
    "themselves, put only the complete command in its own fenced code block "
    "labeled bash (or the appropriate shell). Keep explanations, command "
    "output, and other prose outside that fenced block.";

constexpr const char* kThreadDeckShieldContext =
    "ThreadDeck Shield is enabled for this local thread. A "
    "ThreadDeck-managed sudo command is available in PATH and uses the "
    "user's active privileged authorization. Use sudo when local privileged "
    "work requires it; do not claim that sudo is unavailable merely because "
    "it would normally prompt for a password. Shield is independent of YOLO: "
    "normal Codex approvals and sandbox policy still apply unless YOLO is "
    "also enabled. Shield applies only to this local ThreadDeck process and "
    "does not provide sudo authentication on remote hosts reached through SSH.";

nlohmann::json threaddeck_additional_context(
    bool shield_enabled = false,
    const std::vector<std::string>& remote_shield_hosts = {}
) {
    nlohmann::json context = {
        {
            "threaddeck.command-copy-format",
            {
                {"kind", "application"},
                {"value", kThreadDeckFormattingContext},
            },
        },
    };

    if (shield_enabled) {
        context["threaddeck.shield"] = {
            {"kind", "application"},
            {"value", kThreadDeckShieldContext},
        };
    }

    if (!remote_shield_hosts.empty()) {
        std::string hosts;

        for (const auto& host : remote_shield_hosts) {
            if (!hosts.empty()) {
                hosts += ", ";
            }

            hosts += host;
        }

        context["threaddeck.remote-shield"] = {
            {"kind", "application"},
            {
                "value",
                "ThreadDeck Remote Shield is enabled for this thread on "
                "these SSH destinations: " + hosts + ". SSH login uses the "
                "user's normal OpenSSH keys and host verification. For a "
                "privileged remote command, use the normal form `ssh HOST "
                "sudo COMMAND`; ThreadDeck supplies that host's saved sudo "
                "credential securely. Do not request or print the password, "
                "do not add `sudo -S`, and do not claim remote sudo is "
                "unavailable without attempting the enabled workflow. "
                "Remote Shield is independent of YOLO and local Shield."
            },
        };
    }

    return context;
}

std::string base64_encode(
    const std::array<unsigned char, 16>& bytes
) {
    static constexpr char alphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz"
        "0123456789+/";

    std::string encoded;
    encoded.reserve(24);

    for (std::size_t index = 0; index < bytes.size(); index += 3) {
        const std::size_t remaining =
            bytes.size() - index;
        const std::uint32_t first = bytes[index];
        const std::uint32_t second =
            remaining > 1 ? bytes[index + 1] : 0;
        const std::uint32_t third =
            remaining > 2 ? bytes[index + 2] : 0;
        const std::uint32_t value =
            (first << 16U) |
            (second << 8U) |
            third;

        encoded.push_back(
            alphabet[(value >> 18U) & 0x3fU]);
        encoded.push_back(
            alphabet[(value >> 12U) & 0x3fU]);
        encoded.push_back(
            remaining > 1
                ? alphabet[(value >> 6U) & 0x3fU]
                : '=');
        encoded.push_back(
            remaining > 2
                ? alphabet[value & 0x3fU]
                : '=');
    }

    return encoded;
}

bool send_all(
    int fd,
    const char* data,
    std::size_t size,
    std::string& error
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

        error =
            std::string("socket write failed: ") +
            std::strerror(errno);
        return false;
    }

    return true;
}

int connect_unix_socket(
    const std::string& path
) {
    if (path.size() >= sizeof(sockaddr_un::sun_path)) {
        errno = ENAMETOOLONG;
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
        const int connect_error = errno;
        ::close(fd);
        errno = connect_error;
        return -1;
    }

    return fd;
}

bool websocket_handshake(
    int fd,
    std::string& error
) {
    std::array<unsigned char, 16> nonce{};
    std::random_device random;

    for (auto& byte : nonce) {
        byte = static_cast<unsigned char>(random());
    }

    const std::string request =
        "GET / HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Key: " +
        base64_encode(nonce) +
        "\r\n"
        "Sec-WebSocket-Version: 13\r\n\r\n";

    if (!send_all(fd, request.data(), request.size(), error)) {
        return false;
    }

    std::string response;
    response.reserve(1024);
    const auto deadline =
        std::chrono::steady_clock::now() +
        std::chrono::seconds(10);

    while (
        response.find("\r\n\r\n") ==
            std::string::npos &&
        response.size() < 16384
    ) {
        const auto remaining =
            std::chrono::duration_cast<std::chrono::milliseconds>(
                deadline - std::chrono::steady_clock::now());

        if (remaining.count() <= 0) {
            error = "WebSocket handshake timed out";
            return false;
        }

        pollfd descriptor{
            fd,
            static_cast<short>(POLLIN | POLLHUP),
            0,
        };

        const int poll_result =
            ::poll(
                &descriptor,
                1,
                static_cast<int>(remaining.count()));

        if (poll_result == 0) {
            error = "WebSocket handshake timed out";
            return false;
        }

        if (poll_result < 0) {
            if (errno == EINTR) {
                continue;
            }

            error =
                std::string("WebSocket handshake poll failed: ") +
                std::strerror(errno);
            return false;
        }

        char byte = '\0';
        const ssize_t read_result =
            ::recv(fd, &byte, 1, 0);

        if (read_result == 1) {
            response.push_back(byte);
            continue;
        }

        error = "App Server closed during WebSocket handshake";
        return false;
    }

    if (
        response.rfind("HTTP/1.1 101", 0) != 0 &&
        response.rfind("HTTP/1.0 101", 0) != 0
    ) {
        error =
            "App Server rejected the WebSocket handshake: " +
            response.substr(0, response.find("\r\n"));
        return false;
    }

    return true;
}

std::atomic<unsigned int> app_server_instance_counter{0};

std::filesystem::path sibling_executable(
    const std::string& name
) {
    std::error_code error;
    const auto executable =
        std::filesystem::read_symlink(
            "/proc/self/exe",
            error);

    if (error || executable.empty()) {
        return {};
    }

    return executable.parent_path() / name;
}

void signal_process(
    pid_t pid,
    int signal_number,
    bool process_group
) {
    if (pid <= 0) {
        return;
    }

    if (process_group) {
        ::kill(-pid, signal_number);
    }

    ::kill(pid, signal_number);
}

void terminate_and_reap_process(
    pid_t& pid,
    bool process_group
) {
    if (pid <= 0) {
        pid = -1;
        return;
    }

    const pid_t target = pid;
    signal_process(target, SIGTERM, process_group);

    auto wait_until =
        std::chrono::steady_clock::now() +
        std::chrono::milliseconds(900);

    while (std::chrono::steady_clock::now() < wait_until) {
        int status = 0;
        const pid_t waited =
            ::waitpid(target, &status, WNOHANG);

        if (waited == target || (waited < 0 && errno == ECHILD)) {
            pid = -1;
            return;
        }

        if (waited < 0 && errno != EINTR) {
            pid = -1;
            return;
        }

        ::usleep(20000);
    }

    signal_process(target, SIGKILL, process_group);
    wait_until =
        std::chrono::steady_clock::now() +
        std::chrono::milliseconds(900);

    while (std::chrono::steady_clock::now() < wait_until) {
        int status = 0;
        const pid_t waited =
            ::waitpid(target, &status, WNOHANG);

        if (waited == target || (waited < 0 && errno == ECHILD)) {
            break;
        }

        if (waited < 0 && errno != EINTR) {
            break;
        }

        ::usleep(20000);
    }

    pid = -1;
}

} // namespace

AppServerClient::~AppServerClient() {
    shutdown();
}

bool AppServerClient::start(std::string& error) {
    return start(error, ProcessEnvironment{});
}

bool AppServerClient::start(
    std::string& error,
    const ProcessEnvironment& environment
) {
    if (is_running()) {
        error = "App Server is already running";
        return false;
    }

    const std::filesystem::path runtime_directory =
        threaddeck::runtime_directory();
    std::error_code filesystem_error;
    std::filesystem::create_directories(
        runtime_directory,
        filesystem_error);

    if (filesystem_error) {
        error =
            "Could not create ThreadDeck runtime directory: " +
            filesystem_error.message();
        return false;
    }

    ::chmod(runtime_directory.c_str(), 0700);

    const auto attach_transport =
        [this, &error](int transport_fd) {
            if (!websocket_handshake(transport_fd, error)) {
                ::close(transport_fd);
                return false;
            }

            stdin_fd_ = transport_fd;
            stdout_fd_ = ::dup(transport_fd);

            if (stdout_fd_ < 0) {
                error =
                    std::string("Could not duplicate App Server socket: ") +
                    std::strerror(errno);
                ::close(stdin_fd_);
                stdin_fd_ = -1;
                return false;
            }

            stdout_buffer_.clear();
            websocket_message_buffer_.clear();
            stderr_output_.clear();
            next_request_id_ = 1;
            return true;
        };

    const std::filesystem::path shared_socket_path =
        threaddeck::app_server_socket_path();

    if (environment.tablet_accessible) {
        const int shared_socket =
            connect_unix_socket(
                shared_socket_path.string());

        if (shared_socket >= 0) {
            app_server_socket_path_ =
                shared_socket_path.string();
            owns_app_server_socket_ = false;

            if (!attach_transport(shared_socket)) {
                app_server_socket_path_.clear();
                return false;
            }

            return true;
        }
    }

    app_server_socket_path_ =
        (
            environment.tablet_accessible
                ? threaddeck::app_server_socket_path()
                : runtime_directory /
                    (
                        "app-server-" +
                        std::to_string(::getpid()) +
                        "-" +
                        std::to_string(
                            ++app_server_instance_counter) +
                        ".sock"
                    )
        ).string();
    owns_app_server_socket_ = true;

    const int existing_socket =
        connect_unix_socket(app_server_socket_path_);

    if (existing_socket >= 0) {
        ::close(existing_socket);
        error =
            "Another ThreadDeck App Server is already using " +
            app_server_socket_path_;
        return false;
    }

    if (
        std::filesystem::exists(
            app_server_socket_path_,
            filesystem_error)
    ) {
        if (::unlink(app_server_socket_path_.c_str()) != 0) {
            error =
                std::string("Could not remove stale App Server socket: ") +
                std::strerror(errno);
            return false;
        }
    }

    int child_stdin[2]{};
    int child_stdout[2]{};
    int child_stderr[2]{};

    if (::pipe(child_stdin) != 0) {
        error = std::string("stdin pipe failed: ") + std::strerror(errno);
        return false;
    }

    if (::pipe(child_stdout) != 0) {
        error = std::string("stdout pipe failed: ") + std::strerror(errno);
        ::close(child_stdin[0]);
        ::close(child_stdin[1]);
        return false;
    }

    if (::pipe(child_stderr) != 0) {
        error = std::string("stderr pipe failed: ") + std::strerror(errno);
        ::close(child_stdin[0]);
        ::close(child_stdin[1]);
        ::close(child_stdout[0]);
        ::close(child_stdout[1]);
        return false;
    }

    child_pid_ = ::fork();

    if (child_pid_ < 0) {
        error = std::string("fork failed: ") + std::strerror(errno);

        ::close(child_stdin[0]);
        ::close(child_stdin[1]);
        ::close(child_stdout[0]);
        ::close(child_stdout[1]);
        ::close(child_stderr[0]);
        ::close(child_stderr[1]);

        child_pid_ = -1;
        return false;
    }

    if (child_pid_ == 0) {
        ::setpgid(0, 0);

        ::dup2(child_stdin[0], STDIN_FILENO);
        ::dup2(child_stdout[1], STDOUT_FILENO);
        ::dup2(child_stderr[1], STDERR_FILENO);

        ::close(child_stdin[0]);
        ::close(child_stdin[1]);
        ::close(child_stdout[0]);
        ::close(child_stdout[1]);
        ::close(child_stderr[0]);
        ::close(child_stderr[1]);

        if (environment.manage_splunk) {
            const bool environment_ready =
                (
                    environment.splunk_host.empty()
                        ? ::unsetenv("SPLUNK_HOST")
                        : ::setenv(
                            "SPLUNK_HOST",
                            environment.splunk_host.c_str(),
                            1)
                ) == 0 &&
                (
                    environment.splunk_token.empty()
                        ? ::unsetenv("SPLUNK_TOKEN")
                        : ::setenv(
                            "SPLUNK_TOKEN",
                            environment.splunk_token.c_str(),
                            1)
                ) == 0;

            if (!environment_ready) {
                const std::string message =
                    "Could not configure the Codex process environment\n";

                ::write(
                    STDERR_FILENO,
                    message.data(),
                    message.size());
                _exit(127);
            }
        }

        if (
            environment.shield_enabled ||
            !environment.remote_shield_hosts_json.empty()
        ) {
            const char* inherited_path =
                ::getenv("PATH");
            std::string shield_path;

            if (!environment.remote_shield_hosts_json.empty()) {
                shield_path +=
                    environment.remote_shield_ssh_directory +
                    ":";
            }

            if (environment.shield_enabled) {
                shield_path +=
                    environment.shield_sudo_directory +
                    ":";
            }

            shield_path +=
                (
                    inherited_path == nullptr
                        ? std::string{"/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin"}
                        : inherited_path
                );

            const bool shield_ready =
                (
                    !environment.shield_enabled ||
                    (
                        !environment.shield_sudo_directory.empty() &&
                        !environment.shield_executor_path.empty()
                    )
                ) &&
                (
                    environment.remote_shield_hosts_json.empty() ||
                    !environment.remote_shield_ssh_directory.empty()
                ) &&
                ::setenv(
                    "PATH",
                    shield_path.c_str(),
                    1) == 0;

            const bool local_shield_ready =
                environment.shield_enabled
                    ? ::setenv(
                        "THREADDECK_SHIELD_EXECUTOR",
                        environment.shield_executor_path.c_str(),
                        1) == 0
                    : ::unsetenv(
                        "THREADDECK_SHIELD_EXECUTOR") == 0;

            const bool remote_shield_ready =
                environment.remote_shield_hosts_json.empty()
                    ? ::unsetenv(
                        "THREADDECK_REMOTE_SHIELD_HOSTS") == 0
                    : ::setenv(
                        "THREADDECK_REMOTE_SHIELD_HOSTS",
                        environment.remote_shield_hosts_json.c_str(),
                        1) == 0;

            if (
                !shield_ready ||
                !local_shield_ready ||
                !remote_shield_ready
            ) {
                const std::string message =
                    "Could not configure the ThreadDeck Shield environment\n";

                ::write(
                    STDERR_FILENO,
                    message.data(),
                    message.size());
                _exit(127);
            }
        } else {
            ::unsetenv("THREADDECK_SHIELD_EXECUTOR");
            ::unsetenv("THREADDECK_REMOTE_SHIELD_HOSTS");
        }

        ::execlp(
            "codex",
            "codex",
            "app-server",
            "--listen",
            (
                "unix://" +
                app_server_socket_path_
            ).c_str(),
            static_cast<char*>(nullptr));

        const std::string message =
            std::string("execlp failed: ") + std::strerror(errno) + "\n";

        ::write(STDERR_FILENO, message.data(), message.size());
        _exit(127);
    }

    ::setpgid(child_pid_, child_pid_);

    ::close(child_stdin[0]);
    ::close(child_stdin[1]);
    ::close(child_stdout[0]);
    ::close(child_stdout[1]);
    ::close(child_stderr[1]);

    stderr_fd_ = child_stderr[0];

    const int stderr_flags =
        ::fcntl(stderr_fd_, F_GETFL, 0);

    if (stderr_flags >= 0) {
        ::fcntl(
            stderr_fd_,
            F_SETFL,
            stderr_flags | O_NONBLOCK);
    }
    int transport_fd = -1;
    const auto connection_deadline =
        std::chrono::steady_clock::now() +
        std::chrono::seconds(10);

    while (
        std::chrono::steady_clock::now() <
        connection_deadline
    ) {
        transport_fd =
            connect_unix_socket(
                app_server_socket_path_);

        if (transport_fd >= 0) {
            break;
        }

        int child_status = 0;
        const pid_t wait_result =
            ::waitpid(
                child_pid_,
                &child_status,
                WNOHANG);

        if (wait_result == child_pid_) {
            child_pid_ = -1;
            collect_stderr();
            error =
                "App Server exited before opening its socket";

            if (!stderr_output_.empty()) {
                error += ": " + stderr_output_;
            }

            ::close(stderr_fd_);
            stderr_fd_ = -1;
            return false;
        }

        ::usleep(50000);
    }

    if (transport_fd < 0) {
        error =
            std::string("Could not connect to App Server socket: ") +
            std::strerror(errno);
        shutdown();
        return false;
    }

    if (!attach_transport(transport_fd)) {
        shutdown();
        return false;
    }

    const char* external_tablet_bridge =
        std::getenv("THREADDECK_TABLET_BRIDGE_EXTERNAL");
    const bool should_start_tablet_bridge =
        environment.tablet_accessible &&
        !(
            external_tablet_bridge != nullptr &&
            std::string(external_tablet_bridge) == "1"
        );

    if (should_start_tablet_bridge) {
        const std::filesystem::path bridge_path =
            sibling_executable(
                "threaddeck-tablet-bridge");

        if (
            !bridge_path.empty() &&
            std::filesystem::is_regular_file(
                bridge_path,
                filesystem_error)
        ) {
            tablet_bridge_pid_ = ::fork();

            if (tablet_bridge_pid_ == 0) {
                const std::string port =
                    std::to_string(
                        threaddeck::kTabletBridgePort);

                ::execl(
                    bridge_path.c_str(),
                    bridge_path.c_str(),
                    "--socket",
                    app_server_socket_path_.c_str(),
                    "--port",
                    port.c_str(),
                    static_cast<char*>(nullptr));

                _exit(127);
            }

            if (tablet_bridge_pid_ < 0) {
                tablet_bridge_pid_ = -1;
                std::cerr
                    << "WARN: could not start the ThreadDeck "
                       "tablet bridge: "
                    << std::strerror(errno)
                    << '\n';
            }
        } else {
            std::cerr
                << "WARN: ThreadDeck tablet bridge executable "
                   "was not found beside the application\n";
        }
    }

    return true;
}

AppServerClient::InitializeResult AppServerClient::initialize(
    const std::string& client_name,
    const std::string& client_title,
    const std::string& client_version,
    int timeout_ms) {
    InitializeResult result;

    if (!is_running()) {
        result.error = "App Server is not running";
        return result;
    }

    const int request_id = allocate_request_id();

    const nlohmann::json request = {
        {"id", request_id},
        {"method", "initialize"},
        {"params",
         {
             {"clientInfo",
              {
                  {"name", client_name},
                  {"title", client_title},
                  {"version", client_version},
              }},
             {"capabilities",
              {
                  {"experimentalApi", true},
              }},
         }},
    };

    if (!write_line(request.dump(), result.error)) {
        return result;
    }

    const std::string response_line =
        read_line(timeout_ms, result.error);

    if (response_line.empty()) {
        if (result.error.empty()) {
            result.error =
                "No initialization response was received within the timeout";
        }

        return result;
    }

    try {
        result.response = nlohmann::json::parse(response_line);
    } catch (const nlohmann::json::exception& exception) {
        result.error =
            std::string("Invalid JSON response: ") + exception.what();
        return result;
    }

    if (result.response.value("id", 0) != request_id) {
        result.error = "Initialization response has an unexpected request ID";
        return result;
    }

    if (!result.response.contains("result") ||
        !result.response["result"].is_object()) {
        result.error = "Initialization response does not contain a result";
        return result;
    }

    const auto& response_result = result.response["result"];

    const bool required_fields_present =
        response_result.contains("codexHome") &&
        response_result.contains("platformFamily") &&
        response_result.contains("platformOs") &&
        response_result.contains("userAgent");

    if (!required_fields_present) {
        result.error =
            "Initialization response is missing required fields";
        return result;
    }

    const nlohmann::json initialized_notification = {
        {"method", "initialized"},
    };

    if (!write_line(
            initialized_notification.dump(),
            result.error)) {
        return result;
    }

    result.success = true;
    return result;
}


AppServerClient::RequestResult AppServerClient::request(
    const std::string& method,
    const nlohmann::json& params,
    int timeout_ms) {
    RequestResult result;

    if (!is_running()) {
        result.error = "App Server is not running";
        return result;
    }

    const int request_id = allocate_request_id();

    nlohmann::json request_message = {
        {"id", request_id},
        {"method", method},
    };

    if (!params.is_null()) {
        request_message["params"] = params;
    }

    if (!write_line(
            request_message.dump(),
            result.error)) {
        return result;
    }

    const auto deadline =
        std::chrono::steady_clock::now() +
        std::chrono::milliseconds(timeout_ms);

    while (std::chrono::steady_clock::now() < deadline) {
        const auto remaining =
            std::chrono::duration_cast<
                std::chrono::milliseconds>(
                deadline -
                std::chrono::steady_clock::now());

        const int remaining_ms =
            remaining.count() > 0
                ? static_cast<int>(remaining.count())
                : 1;

        std::string read_error;
        const std::string message_line =
            read_line(remaining_ms, read_error);

        if (message_line.empty()) {
            result.error =
                read_error.empty()
                    ? "No " + method +
                        " response was received within the timeout"
                    : read_error;
            return result;
        }

        nlohmann::json message;

        try {
            message =
                nlohmann::json::parse(message_line);
        } catch (
            const nlohmann::json::exception& exception
        ) {
            result.error =
                std::string("Invalid JSON message: ") +
                exception.what();
            return result;
        }

        const bool matching_response =
            message.contains("id") &&
            message["id"].is_number_integer() &&
            message["id"].get<int>() == request_id;

        if (!matching_response) {
            result.preceding_messages.push_back(
                std::move(message));
            continue;
        }

        result.response = std::move(message);

        if (result.response.contains("error")) {
            result.error =
                method + " returned an error: " +
                result.response["error"].dump();
            return result;
        }

        result.success = true;
        return result;
    }

    result.error =
        "No " + method +
        " response was received within the timeout";
    return result;
}

AppServerClient::ThreadStartResult AppServerClient::start_thread(
    const std::string& cwd,
    bool ephemeral,
    int timeout_ms,
    const SessionOptions& options) {
    ThreadStartResult result;

    if (!is_running()) {
        result.error = "App Server is not running";
        return result;
    }

    const int request_id = allocate_request_id();

    nlohmann::json params = {
        {"cwd", cwd},
        {"ephemeral", ephemeral},
    };

    if (!options.model.empty()) {
        params["model"] = options.model;
    }

    if (!options.reasoning_effort.empty()) {
        params["config"] = {
            {"model_reasoning_effort",
             options.reasoning_effort},
        };
    }

    if (!options.approval_policy.is_null()) {
        params["approvalPolicy"] =
            options.approval_policy;
    }

    if (!options.sandbox_mode.empty()) {
        params["sandbox"] = options.sandbox_mode;
    }

    const nlohmann::json request = {
        {"id", request_id},
        {"method", "thread/start"},
        {"params", params},
    };

    if (!write_line(request.dump(), result.error)) {
        return result;
    }

    const auto deadline =
        std::chrono::steady_clock::now() +
        std::chrono::milliseconds(timeout_ms);

    while (std::chrono::steady_clock::now() < deadline) {
        const auto remaining =
            std::chrono::duration_cast<std::chrono::milliseconds>(
                deadline - std::chrono::steady_clock::now());

        const int remaining_ms =
            remaining.count() > 0
                ? static_cast<int>(remaining.count())
                : 1;

        std::string read_error;
        const std::string message_line =
            read_line(remaining_ms, read_error);

        if (message_line.empty()) {
            result.error =
                read_error.empty()
                    ? "No thread/start response was received within the timeout"
                    : read_error;
            return result;
        }

        nlohmann::json message;

        try {
            message = nlohmann::json::parse(message_line);
        } catch (const nlohmann::json::exception& exception) {
            result.error =
                std::string("Invalid JSON message: ") + exception.what();
            return result;
        }

        const bool matching_response =
            message.contains("id") &&
            message["id"].is_number_integer() &&
            message["id"].get<int>() == request_id;

        if (!matching_response) {
            result.preceding_messages.push_back(std::move(message));
            continue;
        }

        result.response = std::move(message);

        if (result.response.contains("error")) {
            result.error =
                "thread/start returned an error: " +
                result.response["error"].dump();
            return result;
        }

        if (!result.response.contains("result") ||
            !result.response["result"].is_object()) {
            result.error =
                "thread/start response does not contain a result object";
            return result;
        }

        const auto& response_result =
            result.response["result"];

        if (!response_result.contains("thread") ||
            !response_result["thread"].is_object()) {
            result.error =
                "thread/start response does not contain a thread object";
            return result;
        }

        const auto& thread = response_result["thread"];

        if (!thread.contains("id") ||
            !thread["id"].is_string() ||
            thread["id"].get<std::string>().empty()) {
            result.error =
                "thread/start response does not contain a valid thread ID";
            return result;
        }

        result.thread_id =
            thread["id"].get<std::string>();

        result.model =
            response_result.value(
                "model",
                std::string{});

        if (
            response_result.contains(
                "reasoningEffort") &&
            response_result["reasoningEffort"].is_string()
        ) {
            result.reasoning_effort =
                response_result["reasoningEffort"]
                    .get<std::string>();
        }

        if (response_result.contains("approvalPolicy")) {
            result.approval_policy =
                response_result["approvalPolicy"];
        }

        if (response_result.contains("sandbox")) {
            result.sandbox_policy =
                response_result["sandbox"];
        }

        result.success = true;
        return result;
    }

    result.error =
        "No thread/start response was received within the timeout";
    return result;
}



AppServerClient::JsonResult AppServerClient::list_skills(
    const std::string& cwd,
    bool force_reload,
    int timeout_ms) {
    JsonResult result;

    if (cwd.empty()) {
        result.error = "Skills cwd is empty";
        return result;
    }

    const nlohmann::json params = {
        {"cwds", nlohmann::json::array({cwd})},
        {"forceReload", force_reload},
    };

    auto request_result =
        request(
            "skills/list",
            params,
            timeout_ms);

    result.response =
        std::move(request_result.response);

    result.preceding_messages =
        std::move(
            request_result.preceding_messages);

    if (!request_result.success) {
        result.error =
            std::move(request_result.error);
        return result;
    }

    if (
        !result.response.contains("result") ||
        !result.response["result"].is_object()
    ) {
        result.error =
            "skills/list response does not contain a result object";
        return result;
    }

    result.result =
        result.response["result"];

    result.success = true;
    return result;
}


AppServerClient::JsonResult
AppServerClient::run_thread_shell_command(
    const std::string& thread_id,
    const std::string& command,
    int timeout_ms) {
    JsonResult result;

    if (thread_id.empty()) {
        result.error = "Thread ID is empty";
        return result;
    }

    if (command.empty()) {
        result.error = "Shell command is empty";
        return result;
    }

    const nlohmann::json params = {
        {"threadId", thread_id},
        {"command", command},
    };

    auto request_result =
        request(
            "thread/shellCommand",
            params,
            timeout_ms);

    result.response =
        std::move(request_result.response);

    result.preceding_messages =
        std::move(
            request_result.preceding_messages);

    if (!request_result.success) {
        result.error =
            std::move(request_result.error);
        return result;
    }

    if (
        !result.response.contains("result") ||
        !result.response["result"].is_object()
    ) {
        result.error =
            "thread/shellCommand response does not contain a result object";
        return result;
    }

    result.result =
        result.response["result"];

    const auto is_terminal_command_message =
        [&thread_id](
            const nlohmann::json& message
        ) {
            if (
                !message.is_object() ||
                !message.contains("method") ||
                !message["method"].is_string() ||
                message["method"].get<std::string>() !=
                    "item/completed" ||
                !message.contains("params") ||
                !message["params"].is_object()
            ) {
                return false;
            }

            const auto& params = message["params"];

            if (
                params.contains("threadId") &&
                (
                    !params["threadId"].is_string() ||
                    params["threadId"].get<std::string>() !=
                        thread_id
                )
            ) {
                return false;
            }

            return
                params.contains("item") &&
                params["item"].is_object() &&
                params["item"].contains("type") &&
                params["item"]["type"].is_string() &&
                params["item"]["type"]
                    .get<std::string>() ==
                    "commandExecution";
        };

    bool completed = std::any_of(
        result.preceding_messages.begin(),
        result.preceding_messages.end(),
        is_terminal_command_message);

    auto deadline =
        std::chrono::steady_clock::now() +
        std::chrono::milliseconds(timeout_ms);
    std::size_t malformed_message_count = 0;

    while (!completed) {
        const auto now =
            std::chrono::steady_clock::now();

        if (now >= deadline) {
            result.error =
                "No terminal shell command message was "
                "received within the timeout";
            return result;
        }

        const int remaining_ms =
            static_cast<int>(
                std::chrono::duration_cast<
                    std::chrono::milliseconds>(
                    deadline - now)
                    .count());

        std::string read_error;
        const std::string line =
            read_line(remaining_ms, read_error);

        if (line.empty()) {
            result.error = read_error.empty()
                ? "No terminal shell command message was "
                  "received within the timeout"
                : read_error;
            return result;
        }

        nlohmann::json message;

        try {
            message = nlohmann::json::parse(line);
        } catch (const nlohmann::json::exception& exception) {
            ++malformed_message_count;

            std::cerr
                << "WARN: skipped malformed Codex App Server "
                << "message while waiting for shell completion: "
                << exception.what()
                << '\n';

            if (malformed_message_count >= 8) {
                result.error =
                    "Too many malformed App Server messages "
                    "while waiting for shell completion";
                return result;
            }

            continue;
        }

        deadline =
            std::chrono::steady_clock::now() +
            std::chrono::milliseconds(timeout_ms);

        result.preceding_messages.push_back(
            message);
        completed =
            is_terminal_command_message(message);
    }

    result.success = true;
    return result;
}


AppServerClient::JsonResult AppServerClient::list_models(
    int timeout_ms) {
    JsonResult result;

    const nlohmann::json params = {
        {"limit", 100},
        {"includeHidden", false},
    };

    auto request_result =
        request("model/list", params, timeout_ms);

    result.response =
        std::move(request_result.response);
    result.preceding_messages =
        std::move(request_result.preceding_messages);

    if (!request_result.success) {
        result.error =
            std::move(request_result.error);
        return result;
    }

    if (
        !result.response.contains("result") ||
        !result.response["result"].is_object()
    ) {
        result.error =
            "model/list response does not contain a result object";
        return result;
    }

    result.result = result.response["result"];
    result.success = true;
    return result;
}

AppServerClient::JsonResult
AppServerClient::read_account_rate_limits(
    int timeout_ms) {
    JsonResult result;

    auto request_result =
        request(
            "account/rateLimits/read",
            nlohmann::json(nullptr),
            timeout_ms);

    result.response =
        std::move(request_result.response);
    result.preceding_messages =
        std::move(request_result.preceding_messages);

    if (!request_result.success) {
        result.error =
            std::move(request_result.error);
        return result;
    }

    if (
        !result.response.contains("result") ||
        !result.response["result"].is_object()
    ) {
        result.error =
            "account/rateLimits/read response does not contain a result object";
        return result;
    }

    result.result = result.response["result"];
    result.success = true;
    return result;
}

AppServerClient::JsonResult AppServerClient::read_account_usage(
    int timeout_ms) {
    JsonResult result;

    auto request_result =
        request(
            "account/usage/read",
            nlohmann::json(nullptr),
            timeout_ms);

    result.response =
        std::move(request_result.response);
    result.preceding_messages =
        std::move(request_result.preceding_messages);

    if (!request_result.success) {
        result.error =
            std::move(request_result.error);
        return result;
    }

    if (
        !result.response.contains("result") ||
        !result.response["result"].is_object()
    ) {
        result.error =
            "account/usage/read response does not contain a result object";
        return result;
    }

    result.result = result.response["result"];
    result.success = true;
    return result;
}


AppServerClient::ThreadListResult AppServerClient::list_threads(
    const std::string& cwd,
    int limit,
    int timeout_ms,
    const std::string& search_term,
    bool use_state_db_only) {
    ThreadListResult result;

    if (limit <= 0) {
        result.error =
            "thread/list limit must be greater than zero";
        return result;
    }

    nlohmann::json params = {
        {"archived", false},
        {"limit", limit},
        {"sortDirection", "desc"},
        {"sortKey", "recency_at"},
        {"useStateDbOnly", use_state_db_only},
    };

    if (!cwd.empty()) {
        params["cwd"] = cwd;
    }

    if (!search_term.empty()) {
        params["searchTerm"] = search_term;
    }

    auto request_result =
        request("thread/list", params, timeout_ms);

    result.response =
        std::move(request_result.response);

    result.preceding_messages =
        std::move(
            request_result.preceding_messages);

    if (!request_result.success) {
        result.error =
            std::move(request_result.error);
        return result;
    }

    if (
        !result.response.contains("result") ||
        !result.response["result"].is_object()
    ) {
        result.error =
            "thread/list response does not contain a result object";
        return result;
    }

    const auto& response_result =
        result.response["result"];

    if (
        !response_result.contains("data") ||
        !response_result["data"].is_array()
    ) {
        result.error =
            "thread/list result does not contain a data array";
        return result;
    }

    for (const auto& thread :
         response_result["data"]) {
        if (
            !thread.is_object() ||
            !thread.contains("id") ||
            !thread["id"].is_string() ||
            !thread.contains("cwd") ||
            !thread["cwd"].is_string()
        ) {
            result.error =
                "thread/list returned an invalid thread object";
            return result;
        }

        result.threads.push_back(thread);
    }

    if (
        response_result.contains("nextCursor") &&
        response_result["nextCursor"].is_string()
    ) {
        result.next_cursor =
            response_result["nextCursor"]
                .get<std::string>();
    }

    result.success = true;
    return result;
}

AppServerClient::ThreadSearchResult AppServerClient::search_threads(
    const std::string& search_term,
    int limit,
    int timeout_ms,
    const std::string& cursor) {
    ThreadSearchResult result;

    if (search_term.empty()) {
        result.error = "thread/search requires a search term";
        return result;
    }

    if (limit <= 0) {
        result.error =
            "thread/search limit must be greater than zero";
        return result;
    }

    nlohmann::json params = {
        {"archived", false},
        {"limit", limit},
        {"searchTerm", search_term},
        {"sortDirection", "desc"},
        {"sortKey", "recency_at"},
    };

    if (!cursor.empty()) {
        params["cursor"] = cursor;
    }

    auto request_result =
        request("thread/search", params, timeout_ms);

    result.response =
        std::move(request_result.response);
    result.preceding_messages =
        std::move(request_result.preceding_messages);

    if (!request_result.success) {
        result.error =
            std::move(request_result.error);
        return result;
    }

    if (
        !result.response.contains("result") ||
        !result.response["result"].is_object()
    ) {
        result.error =
            "thread/search response does not contain a result object";
        return result;
    }

    const auto& response_result =
        result.response["result"];

    if (
        !response_result.contains("data") ||
        !response_result["data"].is_array()
    ) {
        result.error =
            "thread/search result does not contain a data array";
        return result;
    }

    for (const auto& match : response_result["data"]) {
        if (
            !match.is_object() ||
            !match.contains("thread") ||
            !match["thread"].is_object() ||
            !match["thread"].contains("id") ||
            !match["thread"]["id"].is_string()
        ) {
            result.error =
                "thread/search returned an invalid result";
            return result;
        }

        result.matches.push_back(match);
    }

    if (
        response_result.contains("nextCursor") &&
        response_result["nextCursor"].is_string()
    ) {
        result.next_cursor =
            response_result["nextCursor"]
                .get<std::string>();
    }

    result.success = true;
    return result;
}

AppServerClient::JsonResult AppServerClient::delete_thread(
    const std::string& thread_id,
    int timeout_ms) {
    JsonResult result;

    if (thread_id.empty()) {
        result.error = "Thread ID is empty";
        return result;
    }

    const nlohmann::json params = {
        {"threadId", thread_id},
    };

    auto request_result =
        request("thread/delete", params, timeout_ms);

    result.response =
        std::move(request_result.response);
    result.preceding_messages =
        std::move(request_result.preceding_messages);

    if (!request_result.success) {
        result.error =
            std::move(request_result.error);
        return result;
    }

    if (
        !result.response.contains("result") ||
        !result.response["result"].is_object()
    ) {
        result.error =
            "thread/delete response does not contain a result object";
        return result;
    }

    result.result = result.response["result"];
    result.success = true;
    return result;
}

AppServerClient::JsonResult AppServerClient::update_thread_cwd(
    const std::string& thread_id,
    const std::string& cwd,
    int timeout_ms) {
    JsonResult result;

    if (thread_id.empty()) {
        result.error = "Thread ID is empty";
        return result;
    }

    if (cwd.empty()) {
        result.error = "Thread working directory is empty";
        return result;
    }

    const nlohmann::json params = {
        {"threadId", thread_id},
        {"cwd", cwd},
    };

    auto request_result =
        request(
            "thread/settings/update",
            params,
            timeout_ms);

    result.response =
        std::move(request_result.response);
    result.preceding_messages =
        std::move(request_result.preceding_messages);

    if (!request_result.success) {
        result.error =
            std::move(request_result.error);
        return result;
    }

    if (
        !result.response.contains("result") ||
        !result.response["result"].is_object()
    ) {
        result.error =
            "thread/settings/update response does not contain a result object";
        return result;
    }

    result.result = result.response["result"];
    result.success = true;
    return result;
}

AppServerClient::ThreadResumeResult
AppServerClient::resume_thread(
    const std::string& thread_id,
    int timeout_ms,
    const SessionOptions& options) {
    ThreadResumeResult result;

    if (thread_id.empty()) {
        result.error = "Thread ID is empty";
        return result;
    }

    nlohmann::json params = {
        {"threadId", thread_id},
        {"excludeTurns", false},
    };

    if (!options.cwd.empty()) {
        params["cwd"] = options.cwd;
    }

    if (!options.model.empty()) {
        params["model"] = options.model;
    }

    if (!options.reasoning_effort.empty()) {
        params["config"] = {
            {"model_reasoning_effort",
             options.reasoning_effort},
        };
    }

    if (!options.approval_policy.is_null()) {
        params["approvalPolicy"] =
            options.approval_policy;
    }

    if (!options.sandbox_mode.empty()) {
        params["sandbox"] = options.sandbox_mode;
    }

    auto request_result =
        request("thread/resume", params, timeout_ms);

    result.response =
        std::move(request_result.response);

    result.preceding_messages =
        std::move(
            request_result.preceding_messages);

    if (!request_result.success) {
        result.error =
            std::move(request_result.error);
        return result;
    }

    if (
        !result.response.contains("result") ||
        !result.response["result"].is_object()
    ) {
        result.error =
            "thread/resume response does not contain a result object";
        return result;
    }

    const auto& response_result =
        result.response["result"];

    if (
        !response_result.contains("thread") ||
        !response_result["thread"].is_object()
    ) {
        result.error =
            "thread/resume result does not contain a thread object";
        return result;
    }

    const auto& thread =
        response_result["thread"];

    if (
        !thread.contains("id") ||
        !thread["id"].is_string() ||
        thread["id"].get<std::string>() != thread_id
    ) {
        result.error =
            "thread/resume returned an unexpected thread ID";
        return result;
    }

    if (
        !response_result.contains("cwd") ||
        !response_result["cwd"].is_string()
    ) {
        result.error =
            "thread/resume result does not contain cwd";
        return result;
    }

    if (
        !thread.contains("turns") ||
        !thread["turns"].is_array()
    ) {
        result.error =
            "thread/resume thread does not contain a turns array";
        return result;
    }

    result.thread_id = thread_id;
    result.cwd =
        response_result["cwd"].get<std::string>();

    result.model =
        response_result.value(
            "model",
            std::string{});

    if (
        response_result.contains(
            "reasoningEffort") &&
        response_result["reasoningEffort"].is_string()
    ) {
        result.reasoning_effort =
            response_result["reasoningEffort"]
                .get<std::string>();
    }

    if (response_result.contains("approvalPolicy")) {
        result.approval_policy =
            response_result["approvalPolicy"];
    }

    if (response_result.contains("sandbox")) {
        result.sandbox_policy =
            response_result["sandbox"];
    }

    for (
        const auto& message :
        result.preceding_messages
    ) {
        if (
            !message.is_object() ||
            message.value(
                "method",
                std::string{}) !=
                "thread/settings/updated" ||
            !message.contains("params") ||
            !message["params"].is_object()
        ) {
            continue;
        }

        const auto& update = message["params"];

        if (
            update.value(
                "threadId",
                std::string{}) != thread_id ||
            !update.contains("threadSettings") ||
            !update["threadSettings"].is_object()
        ) {
            continue;
        }

        const auto& settings =
            update["threadSettings"];

        if (
            settings.contains("collaborationMode") &&
            settings["collaborationMode"].is_object()
        ) {
            result.collaboration_mode =
                settings["collaborationMode"].value(
                    "mode",
                    std::string{});
        }
    }

    result.thread = thread;
    result.success = true;
    return result;
}

AppServerClient::ThreadReadResult AppServerClient::read_thread(
    const std::string& thread_id,
    bool include_turns,
    int timeout_ms) {
    ThreadReadResult result;

    if (thread_id.empty()) {
        result.error = "Thread ID is empty";
        return result;
    }

    const nlohmann::json params = {
        {"threadId", thread_id},
        {"includeTurns", include_turns},
    };

    auto request_result =
        request("thread/read", params, timeout_ms);

    result.response =
        std::move(request_result.response);

    result.preceding_messages =
        std::move(
            request_result.preceding_messages);

    if (!request_result.success) {
        result.error =
            std::move(request_result.error);
        return result;
    }

    if (
        !result.response.contains("result") ||
        !result.response["result"].is_object() ||
        !result.response["result"].contains("thread") ||
        !result.response["result"]["thread"].is_object()
    ) {
        result.error =
            "thread/read response does not contain a thread object";
        return result;
    }

    const auto& thread =
        result.response["result"]["thread"];

    if (
        !thread.contains("id") ||
        !thread["id"].is_string() ||
        thread["id"].get<std::string>() != thread_id
    ) {
        result.error =
            "thread/read returned an unexpected thread ID";
        return result;
    }

    if (
        include_turns &&
        (
            !thread.contains("turns") ||
            !thread["turns"].is_array()
        )
    ) {
        result.error =
            "thread/read thread does not contain a turns array";
        return result;
    }

    result.thread_id = thread_id;
    result.thread = thread;
    result.success = true;
    return result;
}

AppServerClient::TurnResult AppServerClient::start_turn(
    const std::string& thread_id,
    const std::string& text,
    int timeout_ms,
    const TurnEventCallback& callback,
    const ApprovalCallback& approval_callback,
    const SessionOptions& options) {
    const nlohmann::json input =
        nlohmann::json::array(
            {
                {
                    {"type", "text"},
                    {"text", text},
                    {"text_elements",
                     nlohmann::json::array()},
                },
            });

    return start_turn_with_input(
        thread_id,
        input,
        timeout_ms,
        callback,
        approval_callback,
        options);
}

AppServerClient::TurnResult
AppServerClient::start_turn_with_input(
    const std::string& thread_id,
    const nlohmann::json& input,
    int timeout_ms,
    const TurnEventCallback& callback,
    const ApprovalCallback& approval_callback,
    const SessionOptions& options) {
    TurnResult result;

    if (!is_running()) {
        result.error = "App Server is not running";
        return result;
    }

    if (thread_id.empty()) {
        result.error = "Thread ID is empty";
        return result;
    }

    if (
        !input.is_array() ||
        input.empty()
    ) {
        result.error =
            "Turn input must be a non-empty array";
        return result;
    }

    nlohmann::json params = {
        {"threadId", thread_id},
        {"input", input},
        {
            "additionalContext",
            threaddeck_additional_context(
                options.shield_enabled,
                options.remote_shield_hosts)
        },
    };

    if (!options.cwd.empty()) {
        params["cwd"] = options.cwd;
    }

    if (!options.model.empty()) {
        params["model"] = options.model;
    }

    if (!options.reasoning_effort.empty()) {
        params["effort"] =
            options.reasoning_effort;
    }

    if (!options.approval_policy.is_null()) {
        params["approvalPolicy"] =
            options.approval_policy;
    }

    if (!options.sandbox_policy.is_null()) {
        params["sandboxPolicy"] =
            options.sandbox_policy;
    }

    return run_streaming_turn_operation(
        thread_id,
        "turn/start",
        params,
        true,
        timeout_ms,
        callback,
        approval_callback);
}

AppServerClient::TurnResult
AppServerClient::compact_thread(
    const std::string& thread_id,
    int timeout_ms,
    const TurnEventCallback& callback
) {
    if (thread_id.empty()) {
        TurnResult result;
        result.error = "Thread ID is empty";
        return result;
    }

    return run_streaming_turn_operation(
        thread_id,
        "thread/compact/start",
        {
            {"threadId", thread_id},
        },
        false,
        timeout_ms,
        callback,
        {});
}

AppServerClient::TurnResult
AppServerClient::run_streaming_turn_operation(
    const std::string& thread_id,
    const std::string& request_method,
    const nlohmann::json& params,
    bool response_contains_turn,
    int timeout_ms,
    const TurnEventCallback& callback,
    const ApprovalCallback& approval_callback
) {
    TurnResult result;

    if (!is_running()) {
        result.error = "App Server is not running";
        return result;
    }

    const int request_id = allocate_request_id();

    const nlohmann::json request = {
        {"id", request_id},
        {"method", request_method},
        {"params", params},
    };

    if (!write_line(request.dump(), result.error)) {
        return result;
    }

    nlohmann::json pending_completion;
    bool turn_started_emitted = false;
    bool request_response_received = false;
    std::size_t malformed_message_count = 0;

    const auto emit_event =
        [&callback](
            TurnEvent::Type type,
            const std::string& event_thread_id,
            const std::string& event_turn_id,
            const std::string& item_id,
            const std::string& delta,
            const nlohmann::json& item,
            const nlohmann::json& message
        ) {
            if (!callback) {
                return;
            }

            TurnEvent event;
            event.type = type;
            event.thread_id = event_thread_id;
            event.turn_id = event_turn_id;
            event.item_id = item_id;
            event.delta = delta;
            event.item = item;
            event.message = message;

            callback(event);
        };

    const auto apply_completion =
        [&result](const nlohmann::json& message) {
            result.completion = message;

            const auto& turn =
                result.completion.at("params").at("turn");

            result.status =
                turn.value("status", std::string{});

            if (
                turn.contains("error") &&
                turn["error"].is_object()
            ) {
                result.turn_error =
                    turn["error"].value(
                        "message",
                        std::string{});
            }

            result.success = !result.status.empty();
        };

    while (true) {
        std::string read_error;
        const std::string message_line =
            read_line(timeout_ms, read_error);

        if (message_line.empty()) {
            if (read_error.empty()) {
                continue;
            }

            result.error = read_error;
            shutdown();
            return result;
        }

        nlohmann::json message;

        try {
            message = nlohmann::json::parse(message_line);
        } catch (const nlohmann::json::exception& exception) {
            ++malformed_message_count;

            std::cerr
                << "WARN: skipped malformed App Server message "
                << malformed_message_count
                << " during active turn ("
                << message_line.size()
                << " byte(s)): "
                << exception.what()
                << '\n';

            continue;
        }

        const std::string incoming_method =
            message.value(
                "method",
                std::string{});

        const bool is_approval_request =
            (
                incoming_method ==
                    "item/commandExecution/requestApproval" ||
                incoming_method ==
                    "item/fileChange/requestApproval" ||
                incoming_method ==
                    "item/permissions/requestApproval"
            ) &&
            message.contains("id") &&
            (
                message["id"].is_string() ||
                message["id"].is_number_integer()
            ) &&
            message.contains("params") &&
            message["params"].is_object();

        if (is_approval_request) {
            result.messages.push_back(message);

            const auto& params =
                message["params"];

            ApprovalRequest approval_request;
            approval_request.request_id =
                message["id"];
            approval_request.method =
                incoming_method;
            approval_request.thread_id =
                params.value(
                    "threadId",
                    std::string{});
            approval_request.turn_id =
                params.value(
                    "turnId",
                    std::string{});
            approval_request.item_id =
                params.value(
                    "itemId",
                    std::string{});
            approval_request.params =
                params;
            approval_request.message =
                message;

            std::string decision{"decline"};

            const bool matching_thread =
                approval_request.thread_id ==
                thread_id;

            const bool matching_turn =
                result.turn_id.empty() ||
                approval_request.turn_id ==
                result.turn_id;

            if (
                matching_thread &&
                matching_turn &&
                approval_callback
            ) {
                decision =
                    approval_callback(
                        approval_request);
            }

            if (
                decision != "accept" &&
                decision != "acceptForSession" &&
                decision != "decline" &&
                decision != "cancel"
            ) {
                decision = "decline";
            }

            nlohmann::json approval_result;

            if (
                incoming_method ==
                "item/permissions/requestApproval"
            ) {
                const bool approved =
                    decision == "accept" ||
                    decision == "acceptForSession";

                approval_result = {
                    {"permissions",
                     approved && params.contains("permissions")
                         ? params["permissions"]
                         : nlohmann::json::object()},
                    {"scope",
                     decision == "acceptForSession"
                         ? "session"
                         : "turn"},
                };
            } else {
                approval_result = {
                    {"decision", decision},
                };
            }

            const nlohmann::json approval_response = {
                {"id", approval_request.request_id},
                {"result", std::move(approval_result)},
            };

            std::string approval_error;

            if (
                !write_line(
                    approval_response.dump(),
                    approval_error)
            ) {
                result.error =
                    "Could not send " +
                    incoming_method +
                    " response: " +
                    approval_error;
                return result;
            }

            continue;
        }

        const bool matching_response =
            message.contains("id") &&
            message["id"].is_number_integer() &&
            message["id"].get<int>() == request_id;

        if (matching_response) {
            result.response = message;

            if (message.contains("error")) {
                result.error =
                    request_method +
                    " returned an error: " +
                    message["error"].dump();
                return result;
            }

            if (
                !message.contains("result") ||
                !message["result"].is_object()
            ) {
                result.error =
                    request_method +
                    " response does not contain a result object";
                return result;
            }

            request_response_received = true;

            if (response_contains_turn) {
                if (
                    !message["result"].contains("turn") ||
                    !message["result"]["turn"].is_object() ||
                    !message["result"]["turn"].contains("id") ||
                    !message["result"]["turn"]["id"].is_string()
                ) {
                    result.error =
                        request_method +
                        " response does not contain a valid turn";
                    return result;
                }

                result.turn_id =
                    message["result"]["turn"]["id"]
                        .get<std::string>();

                if (!turn_started_emitted) {
                    emit_event(
                        TurnEvent::Type::TurnStarted,
                        thread_id,
                        result.turn_id,
                        {},
                        {},
                        nlohmann::json{},
                        message);

                    turn_started_emitted = true;
                }
            }

            if (
                !pending_completion.is_null() &&
                (
                    result.turn_id.empty() ||
                    pending_completion.at("params")
                        .at("turn")
                        .value("id", std::string{}) ==
                        result.turn_id
                )
            ) {
                if (result.turn_id.empty()) {
                    result.turn_id =
                        pending_completion.at("params")
                            .at("turn")
                            .value("id", std::string{});
                }

                apply_completion(pending_completion);
                return result;
            }

            continue;
        }

        int response_id = 0;
        bool is_steer_response = false;
        std::string steer_turn_id;

        if (
            message.contains("id") &&
            message["id"].is_number_integer()
        ) {
            response_id =
                message["id"].get<int>();

            std::lock_guard<std::mutex> lock(
                steer_request_mutex_);

            const auto pending =
                pending_steer_requests_.find(
                    response_id);

            if (
                pending !=
                pending_steer_requests_.end()
            ) {
                is_steer_response = true;
                steer_turn_id = pending->second;
                pending_steer_requests_.erase(
                    pending);
            }
        }

        if (is_steer_response) {
            const bool accepted =
                !message.contains("error") &&
                message.contains("result") &&
                message["result"].is_object() &&
                message["result"].value(
                    "turnId",
                    std::string{}) ==
                    steer_turn_id;

            emit_event(
                accepted
                    ? TurnEvent::Type::SteerAccepted
                    : TurnEvent::Type::SteerRejected,
                thread_id,
                steer_turn_id,
                {},
                {},
                nlohmann::json{},
                message);
            continue;
        }

        result.messages.push_back(message);

        const std::string method =
            message.value("method", std::string{});

        if (
            (
                method == "thread/tokenUsage/updated" ||
                method == "thread/settings/updated"
            ) &&
            message.contains("params") &&
            message["params"].is_object() &&
            message["params"].value(
                "threadId",
                std::string{}) == thread_id
        ) {
            emit_event(
                method == "thread/tokenUsage/updated"
                    ? TurnEvent::Type::TokenUsageUpdated
                    : TurnEvent::Type::ThreadSettingsUpdated,
                thread_id,
                message["params"].value(
                    "turnId",
                    std::string{}),
                {},
                {},
                nlohmann::json{},
                message);
            continue;
        }

        if (
            method == "account/rateLimits/updated" &&
            message.contains("params") &&
            message["params"].is_object()
        ) {
            emit_event(
                TurnEvent::Type::AccountRateLimitsUpdated,
                thread_id,
                result.turn_id,
                {},
                {},
                nlohmann::json{},
                message);
            continue;
        }

        if (
            method == "turn/started" &&
            message.contains("params") &&
            message["params"].is_object()
        ) {
            const auto& params = message["params"];

            if (
                params.value(
                    "threadId",
                    std::string{}) == thread_id &&
                params.contains("turn") &&
                params["turn"].is_object()
            ) {
                const std::string started_turn_id =
                    params["turn"].value(
                        "id",
                        std::string{});

                if (!started_turn_id.empty()) {
                    if (result.turn_id.empty()) {
                        result.turn_id =
                            started_turn_id;
                    }

                    if (!turn_started_emitted) {
                        emit_event(
                            TurnEvent::Type::TurnStarted,
                            thread_id,
                            started_turn_id,
                            {},
                            {},
                            nlohmann::json{},
                            message);

                        turn_started_emitted = true;
                    }
                }
            }

            continue;
        }

        if (
            (
                method ==
                    "item/reasoning/summaryTextDelta" ||
                method ==
                    "item/reasoning/textDelta"
            ) &&
            message.contains("params") &&
            message["params"].is_object()
        ) {
            const auto& params = message["params"];

            const std::string event_turn_id =
                params.value(
                    "turnId",
                    std::string{});

            const bool matching_thread =
                params.value(
                    "threadId",
                    std::string{}) == thread_id;

            const bool matching_turn =
                result.turn_id.empty() ||
                event_turn_id == result.turn_id;

            if (
                matching_thread &&
                matching_turn &&
                params.contains("delta") &&
                params["delta"].is_string()
            ) {
                emit_event(
                    method ==
                        "item/reasoning/summaryTextDelta"
                        ? TurnEvent::Type::
                            ReasoningSummaryDelta
                        : TurnEvent::Type::
                            ReasoningTextDelta,
                    thread_id,
                    event_turn_id,
                    params.value(
                        "itemId",
                        std::string{}),
                    params["delta"].get<
                        std::string>(),
                    nlohmann::json{},
                    message);
            }

            continue;
        }

        if (
            (
                method == "item/plan/delta" ||
                method ==
                    "item/commandExecution/outputDelta"
            ) &&
            message.contains("params") &&
            message["params"].is_object()
        ) {
            const auto& params = message["params"];

            const std::string event_turn_id =
                params.value(
                    "turnId",
                    std::string{});

            const bool matching_thread =
                params.value(
                    "threadId",
                    std::string{}) == thread_id;

            const bool matching_turn =
                result.turn_id.empty() ||
                event_turn_id == result.turn_id;

            if (
                matching_thread &&
                matching_turn &&
                params.contains("delta") &&
                params["delta"].is_string()
            ) {
                emit_event(
                    method == "item/plan/delta"
                        ? TurnEvent::Type::PlanDelta
                        : TurnEvent::Type::
                            CommandExecutionOutputDelta,
                    thread_id,
                    event_turn_id,
                    params.value(
                        "itemId",
                        std::string{}),
                    params["delta"].get<std::string>(),
                    nlohmann::json{},
                    message);
            }

            continue;
        }

        if (
            (
                method == "item/started" ||
                method == "item/completed"
            ) &&
            message.contains("params") &&
            message["params"].is_object()
        ) {
            const auto& params = message["params"];

            const std::string event_turn_id =
                params.value(
                    "turnId",
                    std::string{});

            const bool matching_thread =
                params.value(
                    "threadId",
                    std::string{}) == thread_id;

            const bool matching_turn =
                result.turn_id.empty() ||
                event_turn_id == result.turn_id;

            if (
                matching_thread &&
                matching_turn &&
                params.contains("item") &&
                params["item"].is_object()
            ) {
                const auto& item =
                    params["item"];

                emit_event(
                    method == "item/started"
                        ? TurnEvent::Type::ItemStarted
                        : TurnEvent::Type::ItemCompleted,
                    thread_id,
                    event_turn_id,
                    item.value(
                        "id",
                        std::string{}),
                    {},
                    item,
                    message);
            }

            continue;
        }

        if (
            method == "item/agentMessage/delta" &&
            message.contains("params") &&
            message["params"].is_object()
        ) {
            const auto& params = message["params"];

            const bool matching_thread =
                params.value(
                    "threadId",
                    std::string{}) == thread_id;

            const std::string message_turn_id =
                params.value(
                    "turnId",
                    std::string{});

            const bool matching_turn =
                result.turn_id.empty() ||
                message_turn_id == result.turn_id;

            if (
                matching_thread &&
                matching_turn &&
                params.contains("delta") &&
                params["delta"].is_string()
            ) {
                const std::string delta =
                    params["delta"].get<std::string>();

                result.streamed_text += delta;

                emit_event(
                    TurnEvent::Type::AgentMessageDelta,
                    thread_id,
                    message_turn_id,
                    params.value(
                        "itemId",
                        std::string{}),
                    delta,
                    nlohmann::json{},
                    message);
            }

            continue;
        }

        if (
            method == "turn/completed" &&
            message.contains("params") &&
            message["params"].is_object() &&
            message["params"].value(
                "threadId",
                std::string{}) == thread_id &&
            message["params"].contains("turn") &&
            message["params"]["turn"].is_object()
        ) {
            const std::string completed_turn_id =
                message["params"]["turn"].value(
                    "id",
                    std::string{});

            if (completed_turn_id.empty()) {
                continue;
            }

            if (result.turn_id.empty()) {
                result.turn_id = completed_turn_id;
            }

            if (!request_response_received) {
                pending_completion = message;
                continue;
            }

            if (completed_turn_id == result.turn_id) {
                apply_completion(message);
                return result;
            }
        }
    }

}

AppServerClient::InterruptResult
AppServerClient::interrupt_turn(
    const std::string& thread_id,
    const std::string& turn_id
) {
    InterruptResult result;

    if (!is_running()) {
        result.error =
            "App Server is not running";
        return result;
    }

    if (thread_id.empty()) {
        result.error =
            "Thread ID is empty";
        return result;
    }

    if (turn_id.empty()) {
        result.error =
            "Turn ID is empty";
        return result;
    }

    result.request_id =
        allocate_request_id();

    const nlohmann::json request = {
        {"id", result.request_id},
        {"method", "turn/interrupt"},
        {"params",
         {
             {"threadId", thread_id},
             {"turnId", turn_id},
         }},
    };

    if (!write_line(
            request.dump(),
            result.error)) {
        return result;
    }

    result.success = true;
    return result;
}

AppServerClient::SteerResult
AppServerClient::steer_turn(
    const std::string& thread_id,
    const std::string& expected_turn_id,
    const nlohmann::json& input
) {
    SteerResult result;

    if (!is_running()) {
        result.error =
            "App Server is not running";
        return result;
    }

    if (thread_id.empty()) {
        result.error = "Thread ID is empty";
        return result;
    }

    if (expected_turn_id.empty()) {
        result.error = "Expected turn ID is empty";
        return result;
    }

    if (!input.is_array() || input.empty()) {
        result.error =
            "Turn steering input must be a non-empty array";
        return result;
    }

    result.request_id = allocate_request_id();

    const nlohmann::json request = {
        {"id", result.request_id},
        {"method", "turn/steer"},
        {"params",
         {
             {"threadId", thread_id},
             {"expectedTurnId", expected_turn_id},
             {"input", input},
             {"additionalContext", threaddeck_additional_context()},
         }},
    };

    {
        std::lock_guard<std::mutex> lock(
            steer_request_mutex_);
        pending_steer_requests_[result.request_id] =
            expected_turn_id;
    }

    if (!write_line(request.dump(), result.error)) {
        std::lock_guard<std::mutex> lock(
            steer_request_mutex_);
        pending_steer_requests_.erase(
            result.request_id);
        return result;
    }

    result.success = true;
    return result;
}

void AppServerClient::cancel_pending_operation() {
    if (stdin_fd_ >= 0) {
        ::shutdown(stdin_fd_, SHUT_RDWR);
    }

    if (stdout_fd_ >= 0 && stdout_fd_ != stdin_fd_) {
        ::shutdown(stdout_fd_, SHUT_RDWR);
    }

    signal_process(child_pid_, SIGTERM, true);
    signal_process(tablet_bridge_pid_, SIGTERM, false);
}

void AppServerClient::shutdown() {
    cancel_pending_operation();

    terminate_and_reap_process(
        tablet_bridge_pid_,
        false);

    if (stdin_fd_ >= 0) {
        ::close(stdin_fd_);
        stdin_fd_ = -1;
    }

    terminate_and_reap_process(
        child_pid_,
        true);

    collect_stderr();

    if (stdout_fd_ >= 0) {
        ::close(stdout_fd_);
        stdout_fd_ = -1;
    }

    if (stderr_fd_ >= 0) {
        ::close(stderr_fd_);
        stderr_fd_ = -1;
    }

    stdout_buffer_.clear();
    websocket_message_buffer_.clear();

    if (
        owns_app_server_socket_ &&
        !app_server_socket_path_.empty()
    ) {
        ::unlink(app_server_socket_path_.c_str());
    }

    app_server_socket_path_.clear();
    owns_app_server_socket_ = false;

    {
        std::lock_guard<std::mutex> lock(
            steer_request_mutex_);
        pending_steer_requests_.clear();
    }
}

bool AppServerClient::is_running() const {
    return
        stdin_fd_ >= 0 &&
        stdout_fd_ >= 0;
}

const std::string& AppServerClient::stderr_output() const {
    return stderr_output_;
}

int AppServerClient::allocate_request_id() {
    std::lock_guard<std::mutex> lock(
        request_id_mutex_);

    return next_request_id_++;
}

bool AppServerClient::write_line(
    const std::string& line,
    std::string& error) {
    std::lock_guard<std::mutex> lock(
        write_mutex_);

    return write_websocket_frame(
        0x01,
        line,
        error);
}

bool AppServerClient::write_websocket_frame(
    std::uint8_t opcode,
    const std::string& payload,
    std::string& error
) {
    if (stdin_fd_ < 0) {
        error = "App Server WebSocket is not connected";
        return false;
    }

    std::array<unsigned char, 4> mask{};
    std::random_device random;

    for (auto& byte : mask) {
        byte = static_cast<unsigned char>(random());
    }

    std::string frame;
    frame.reserve(payload.size() + 14);
    frame.push_back(
        static_cast<char>(0x80U | (opcode & 0x0fU)));

    const std::uint64_t payload_size =
        static_cast<std::uint64_t>(payload.size());

    if (payload_size <= 125U) {
        frame.push_back(
            static_cast<char>(0x80U | payload_size));
    } else if (payload_size <= 0xffffU) {
        frame.push_back(static_cast<char>(0x80U | 126U));
        frame.push_back(
            static_cast<char>((payload_size >> 8U) & 0xffU));
        frame.push_back(
            static_cast<char>(payload_size & 0xffU));
    } else {
        frame.push_back(static_cast<char>(0x80U | 127U));

        for (int shift = 56; shift >= 0; shift -= 8) {
            frame.push_back(
                static_cast<char>(
                    (payload_size >> shift) & 0xffU));
        }
    }

    for (const auto byte : mask) {
        frame.push_back(static_cast<char>(byte));
    }

    for (std::size_t index = 0; index < payload.size(); ++index) {
        frame.push_back(
            static_cast<char>(
                static_cast<unsigned char>(payload[index]) ^
                mask[index % mask.size()]));
    }

    return send_all(
        stdin_fd_,
        frame.data(),
        frame.size(),
        error);
}

std::string AppServerClient::read_line(
    int timeout_ms,
    std::string& error) {
    constexpr std::uint64_t maximum_message_size =
        64ULL * 1024ULL * 1024ULL;
    const auto deadline =
        std::chrono::steady_clock::now() +
        std::chrono::milliseconds(timeout_ms);

    while (true) {
        if (stdout_buffer_.size() >= 2) {
            const auto first =
                static_cast<unsigned char>(stdout_buffer_[0]);
            const auto second =
                static_cast<unsigned char>(stdout_buffer_[1]);
            const bool finished = (first & 0x80U) != 0;
            const std::uint8_t opcode = first & 0x0fU;
            const bool masked = (second & 0x80U) != 0;
            std::uint64_t payload_size = second & 0x7fU;
            std::size_t header_size = 2;

            if (payload_size == 126U) {
                if (stdout_buffer_.size() < 4) {
                    goto read_more;
                }

                payload_size =
                    (
                        static_cast<std::uint64_t>(
                            static_cast<unsigned char>(
                                stdout_buffer_[2])) << 8U
                    ) |
                    static_cast<std::uint64_t>(
                        static_cast<unsigned char>(
                            stdout_buffer_[3]));
                header_size = 4;
            } else if (payload_size == 127U) {
                if (stdout_buffer_.size() < 10) {
                    goto read_more;
                }

                payload_size = 0;

                for (std::size_t index = 2; index < 10; ++index) {
                    payload_size =
                        (payload_size << 8U) |
                        static_cast<std::uint64_t>(
                            static_cast<unsigned char>(
                                stdout_buffer_[index]));
                }

                header_size = 10;
            }

            if (payload_size > maximum_message_size) {
                error =
                    "App Server WebSocket message exceeds 64 MiB";
                return {};
            }

            std::array<unsigned char, 4> mask{};

            if (masked) {
                if (
                    stdout_buffer_.size() <
                    header_size + mask.size()
                ) {
                    goto read_more;
                }

                for (std::size_t index = 0; index < mask.size(); ++index) {
                    mask[index] =
                        static_cast<unsigned char>(
                            stdout_buffer_[header_size + index]);
                }

                header_size += mask.size();
            }

            if (
                payload_size >
                static_cast<std::uint64_t>(
                    std::string{}.max_size() - header_size)
            ) {
                error = "App Server WebSocket frame is too large";
                return {};
            }

            const std::size_t frame_size =
                header_size +
                static_cast<std::size_t>(payload_size);

            if (stdout_buffer_.size() < frame_size) {
                goto read_more;
            }

            std::string payload =
                stdout_buffer_.substr(
                    header_size,
                    static_cast<std::size_t>(payload_size));
            stdout_buffer_.erase(0, frame_size);

            if (masked) {
                for (std::size_t index = 0; index < payload.size(); ++index) {
                    payload[index] =
                        static_cast<char>(
                            static_cast<unsigned char>(payload[index]) ^
                            mask[index % mask.size()]);
                }
            }

            if (opcode == 0x08U) {
                error = "App Server closed the WebSocket";
                return {};
            }

            if (opcode == 0x09U) {
                std::lock_guard<std::mutex> lock(
                    write_mutex_);

                if (
                    !write_websocket_frame(
                        0x0a,
                        payload,
                        error)
                ) {
                    return {};
                }

                continue;
            }

            if (opcode == 0x0aU) {
                continue;
            }

            if (opcode == 0x01U) {
                websocket_message_buffer_ =
                    std::move(payload);
            } else if (opcode == 0x00U) {
                websocket_message_buffer_ += payload;
            } else {
                error =
                    "App Server sent an unsupported WebSocket frame";
                return {};
            }

            if (finished) {
                std::string message =
                    std::move(websocket_message_buffer_);
                websocket_message_buffer_.clear();
                return message;
            }

            continue;
        }

read_more:
        const auto remaining =
            std::chrono::duration_cast<std::chrono::milliseconds>(
                deadline - std::chrono::steady_clock::now());

        if (remaining.count() <= 0) {
            return {};
        }

        pollfd descriptor{
            stdout_fd_,
            static_cast<short>(POLLIN | POLLHUP),
            0,
        };

        const int poll_result =
            ::poll(
                &descriptor,
                1,
                static_cast<int>(remaining.count()));

        if (poll_result == 0) {
            return {};
        }

        if (poll_result < 0) {
            if (errno == EINTR) {
                continue;
            }

            error =
                std::string("WebSocket poll failed: ") +
                std::strerror(errno);
            return {};
        }

        if (
            (descriptor.revents &
             (POLLIN | POLLHUP | POLLERR)) == 0
        ) {
            continue;
        }

        char buffer[16384];
        const ssize_t read_result =
            ::recv(
                stdout_fd_,
                buffer,
                sizeof(buffer),
                0);

        if (read_result > 0) {
            stdout_buffer_.append(
                buffer,
                static_cast<std::size_t>(read_result));
            continue;
        }

        if (read_result == 0) {
            error = "App Server closed the WebSocket";
            return {};
        }

        if (errno != EINTR) {
            error =
                std::string("WebSocket read failed: ") +
                std::strerror(errno);
            return {};
        }
    }
}

void AppServerClient::collect_stderr() {
    if (stderr_fd_ < 0) {
        return;
    }

    char buffer[4096];

    while (true) {
        const ssize_t result =
            ::read(stderr_fd_, buffer, sizeof(buffer));

        if (result > 0) {
            stderr_output_.append(
                buffer,
                static_cast<std::size_t>(result));
            continue;
        }

        if (result < 0 && errno == EINTR) {
            continue;
        }

        break;
    }
}
