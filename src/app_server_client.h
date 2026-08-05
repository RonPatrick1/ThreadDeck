#pragma once

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

#include <sys/types.h>

class AppServerClient final {
public:
    struct InitializeResult {
        bool success{false};
        nlohmann::json response;
        std::string error;
    };

    struct ThreadStartResult {
        bool success{false};
        std::string thread_id;
        nlohmann::json response;
        std::vector<nlohmann::json> preceding_messages;
        std::string error;
    };

    struct ThreadListResult {
        bool success{false};
        std::vector<nlohmann::json> threads;
        std::string next_cursor;
        nlohmann::json response;
        std::vector<nlohmann::json> preceding_messages;
        std::string error;
    };

    struct ThreadResumeResult {
        bool success{false};
        std::string thread_id;
        std::string cwd;
        nlohmann::json thread;
        nlohmann::json response;
        std::vector<nlohmann::json> preceding_messages;
        std::string error;
    };

    struct ThreadReadResult {
        bool success{false};
        std::string thread_id;
        nlohmann::json thread;
        nlohmann::json response;
        std::vector<nlohmann::json> preceding_messages;
        std::string error;
    };

    struct TurnResult {
        bool success{false};
        std::string turn_id;
        std::string status;
        std::string streamed_text;
        std::string turn_error;
        nlohmann::json response;
        nlohmann::json completion;
        std::vector<nlohmann::json> messages;
        std::string error;
    };

    AppServerClient() = default;
    ~AppServerClient();

    AppServerClient(const AppServerClient&) = delete;
    AppServerClient& operator=(const AppServerClient&) = delete;
    AppServerClient(AppServerClient&&) = delete;
    AppServerClient& operator=(AppServerClient&&) = delete;

    bool start(std::string& error);
    InitializeResult initialize(
        const std::string& client_name,
        const std::string& client_title,
        const std::string& client_version,
        int timeout_ms = 10000);

    ThreadStartResult start_thread(
        const std::string& cwd,
        bool ephemeral = true,
        int timeout_ms = 10000);

    ThreadListResult list_threads(
        const std::string& cwd,
        int limit = 100,
        int timeout_ms = 10000);

    ThreadResumeResult resume_thread(
        const std::string& thread_id,
        int timeout_ms = 10000);

    ThreadReadResult read_thread(
        const std::string& thread_id,
        bool include_turns = true,
        int timeout_ms = 10000);

    TurnResult start_turn(
        const std::string& thread_id,
        const std::string& text,
        int timeout_ms = 60000);

    void shutdown();

    bool is_running() const;
    const std::string& stderr_output() const;

private:
    struct RequestResult {
        bool success{false};
        nlohmann::json response;
        std::vector<nlohmann::json> preceding_messages;
        std::string error;
    };

    RequestResult request(
        const std::string& method,
        const nlohmann::json& params,
        int timeout_ms);

    bool write_line(const std::string& line, std::string& error);
    std::string read_line(int timeout_ms, std::string& error);
    void collect_stderr();

    pid_t child_pid_{-1};
    int stdin_fd_{-1};
    int stdout_fd_{-1};
    int stderr_fd_{-1};
    int next_request_id_{1};
    std::string stderr_output_;
};
