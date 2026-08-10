#include "app_server_client.h"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstring>
#include <iostream>
#include <string>

#include <poll.h>
#include <sys/wait.h>
#include <unistd.h>

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

        if (environment.shield_enabled) {
            const char* inherited_path =
                ::getenv("PATH");
            const std::string shield_path =
                environment.shield_sudo_directory +
                ":" +
                (
                    inherited_path == nullptr
                        ? std::string{"/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin"}
                        : inherited_path
                );

            const bool shield_ready =
                !environment.shield_sudo_directory.empty() &&
                !environment.shield_executor_path.empty() &&
                ::setenv(
                    "PATH",
                    shield_path.c_str(),
                    1) == 0 &&
                ::setenv(
                    "THREADDECK_SHIELD_EXECUTOR",
                    environment.shield_executor_path.c_str(),
                    1) == 0;

            if (!shield_ready) {
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
        }

        ::execlp(
            "codex",
            "codex",
            "app-server",
            "--stdio",
            static_cast<char*>(nullptr));

        const std::string message =
            std::string("execlp failed: ") + std::strerror(errno) + "\n";

        ::write(STDERR_FILENO, message.data(), message.size());
        _exit(127);
    }

    ::close(child_stdin[0]);
    ::close(child_stdout[1]);
    ::close(child_stderr[1]);

    stdin_fd_ = child_stdin[1];
    stdout_fd_ = child_stdout[0];
    stderr_fd_ = child_stderr[0];
    stdout_buffer_.clear();
    stderr_output_.clear();
    next_request_id_ = 1;

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
                    "item/fileChange/requestApproval"
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

            const nlohmann::json approval_response = {
                {"id", approval_request.request_id},
                {"result",
                 {
                     {"decision", decision},
                 }},
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

void AppServerClient::shutdown() {
    if (stdin_fd_ >= 0) {
        ::close(stdin_fd_);
        stdin_fd_ = -1;
    }

    if (child_pid_ > 0) {
        ::kill(child_pid_, SIGTERM);

        int child_status = 0;

        while (::waitpid(child_pid_, &child_status, 0) < 0) {
            if (errno != EINTR) {
                break;
            }
        }

        child_pid_ = -1;
    }

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

    {
        std::lock_guard<std::mutex> lock(
            steer_request_mutex_);
        pending_steer_requests_.clear();
    }
}

bool AppServerClient::is_running() const {
    return child_pid_ > 0;
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

    const std::string data = line + "\n";
    std::size_t written = 0;

    while (written < data.size()) {
        const ssize_t result =
            ::write(
                stdin_fd_,
                data.data() + written,
                data.size() - written);

        if (result > 0) {
            written += static_cast<std::size_t>(result);
            continue;
        }

        if (result < 0 && errno == EINTR) {
            continue;
        }

        error = std::string("write failed: ") + std::strerror(errno);
        return false;
    }

    return true;
}

std::string AppServerClient::read_line(
    int timeout_ms,
    std::string& error) {
    const auto deadline =
        std::chrono::steady_clock::now() +
        std::chrono::milliseconds(timeout_ms);

    const auto take_buffered_line =
        [this](std::string& line) {
            while (true) {
                const std::size_t newline =
                    stdout_buffer_.find('\n');

                if (newline == std::string::npos) {
                    return false;
                }

                line = stdout_buffer_.substr(0, newline);
                stdout_buffer_.erase(0, newline + 1);

                if (
                    !line.empty() &&
                    line.back() == '\r'
                ) {
                    line.pop_back();
                }

                if (!line.empty()) {
                    return true;
                }
            }
        };

    std::string line;

    if (take_buffered_line(line)) {
        return line;
    }

    while (std::chrono::steady_clock::now() < deadline) {
        const auto remaining =
            std::chrono::duration_cast<std::chrono::milliseconds>(
                deadline - std::chrono::steady_clock::now());

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

            error = std::string("poll failed: ") + std::strerror(errno);
            return {};
        }

        if ((descriptor.revents & (POLLIN | POLLHUP)) == 0) {
            continue;
        }

        char buffer[4096];
        const ssize_t read_result =
            ::read(
                stdout_fd_,
                buffer,
                sizeof(buffer));

        if (read_result > 0) {
            stdout_buffer_.append(
                buffer,
                static_cast<std::size_t>(
                    read_result));

            if (take_buffered_line(line)) {
                return line;
            }

            continue;
        }

        if (read_result == 0) {
            error = stdout_buffer_.empty()
                ? "App Server closed stdout"
                : "App Server closed stdout with an incomplete message";
            return {};
        }

        if (errno != EINTR) {
            error = std::string("read failed: ") + std::strerror(errno);
            return {};
        }
    }

    return {};
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
