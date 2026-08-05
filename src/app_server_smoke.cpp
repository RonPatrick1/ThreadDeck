#include "app_server_client.h"

#include <algorithm>
#include <iostream>
#include <string>

int main() {
    AppServerClient client;
    std::string start_error;

    std::cout << "=== START APP SERVER ===\n";

    if (!client.start(start_error)) {
        std::cout
            << "FAIL: "
            << start_error
            << '\n';
        return 1;
    }

    std::cout << "PASS: App Server started\n";

    std::cout << "=== INITIALIZE ===\n";

    const auto initialize_result =
        client.initialize(
            "threaddeck",
            "ThreadDeck",
            "0.1.0");

    if (!initialize_result.success) {
        std::cout
            << "FAIL: "
            << initialize_result.error
            << '\n';

        client.shutdown();
        return 1;
    }

    std::cout << "PASS: initialized\n";

    std::cout << "=== LIST ALL MATERIALIZED THREADS ===\n";

    const auto all_threads =
        client.list_threads("", 100);

    if (!all_threads.success) {
        std::cout
            << "FAIL: "
            << all_threads.error
            << '\n';

        client.shutdown();
        return 1;
    }

    std::cout
        << "PASS: listed "
        << all_threads.threads.size()
        << " materialized thread(s)\n";

    const auto selected =
        std::find_if(
            all_threads.threads.begin(),
            all_threads.threads.end(),
            [](const nlohmann::json& thread) {
                return (
                    thread.is_object() &&
                    thread.value("ephemeral", true) == false &&
                    thread.contains("id") &&
                    thread["id"].is_string() &&
                    !thread["id"].get<std::string>().empty() &&
                    thread.contains("cwd") &&
                    thread["cwd"].is_string() &&
                    !thread["cwd"].get<std::string>().empty()
                );
            });

    if (selected == all_threads.threads.end()) {
        std::cout
            << "SKIPPED: no materialized persistent thread "
            << "was available for validation\n";

        client.shutdown();

        std::cout << "=== APP SERVER STDERR ===\n";
        std::cout << (
            client.stderr_output().empty()
                ? "(none)\n"
                : client.stderr_output());

        std::cout << "=== RESULT ===\n";
        std::cout
            << "SKIPPED: persistent-thread APIs require "
            << "an existing materialized thread\n";

        return 2;
    }

    const std::string thread_id =
        selected->at("id").get<std::string>();

    const std::string cwd =
        selected->at("cwd").get<std::string>();

    std::cout
        << "PASS: selected thread "
        << thread_id
        << '\n';

    std::cout
        << "THREAD CWD: "
        << cwd
        << '\n';

    std::cout << "=== EXACT CWD FILTER ===\n";

    const auto cwd_threads =
        client.list_threads(cwd, 100);

    const bool selected_is_in_cwd_list =
        cwd_threads.success &&
        std::any_of(
            cwd_threads.threads.begin(),
            cwd_threads.threads.end(),
            [&thread_id](const nlohmann::json& thread) {
                return (
                    thread.value(
                        "id",
                        std::string{}) ==
                    thread_id
                );
            });

    if (selected_is_in_cwd_list) {
        std::cout
            << "PASS: exact cwd filter returned "
            << "the selected thread\n";
    } else {
        std::cout
            << "FAIL: exact cwd filter did not return "
            << "the selected thread\n";

        if (!cwd_threads.success) {
            std::cout
                << "FILTER ERROR: "
                << cwd_threads.error
                << '\n';
        }
    }

    std::cout << "=== THREAD RESUME ===\n";

    const auto resume_result =
        client.resume_thread(thread_id);

    if (resume_result.success) {
        std::cout
            << "PASS: resumed thread "
            << resume_result.thread_id
            << " with "
            << resume_result.thread["turns"].size()
            << " turn(s)\n";
    } else {
        std::cout
            << "FAIL: "
            << resume_result.error
            << '\n';
    }

    std::cout << "=== THREAD READ ===\n";

    AppServerClient::ThreadReadResult read_result;

    if (resume_result.success) {
        read_result =
            client.read_thread(thread_id, true);
    }

    if (read_result.success) {
        std::cout
            << "PASS: read thread "
            << read_result.thread_id
            << " with "
            << read_result.thread["turns"].size()
            << " turn(s)\n";
    } else {
        std::cout
            << "FAIL: "
            << (
                read_result.error.empty()
                    ? "resume did not succeed"
                    : read_result.error
            )
            << '\n';
    }

    client.shutdown();

    std::cout << "=== APP SERVER STDERR ===\n";
    std::cout << (
        client.stderr_output().empty()
            ? "(none)\n"
            : client.stderr_output());

    const bool resume_valid =
        resume_result.success &&
        resume_result.thread_id == thread_id &&
        resume_result.cwd == cwd &&
        resume_result.thread.value(
            "ephemeral",
            true) == false &&
        resume_result.thread.contains("turns") &&
        resume_result.thread["turns"].is_array();

    const bool read_valid =
        read_result.success &&
        read_result.thread_id == thread_id &&
        read_result.thread.value(
            "cwd",
            std::string{}) == cwd &&
        read_result.thread.value(
            "ephemeral",
            true) == false &&
        read_result.thread.contains("turns") &&
        read_result.thread["turns"].is_array();

    std::cout << "=== RESULT ===\n";

    if (
        selected_is_in_cwd_list &&
        resume_valid &&
        read_valid &&
        client.stderr_output().empty()
    ) {
        std::cout
            << "PASS: list, exact-cwd filter, resume, "
            << "and read APIs work for a materialized thread\n";
        return 0;
    }

    std::cout
        << "FAIL: materialized persistent-thread "
        << "API validation failed\n";

    std::cout
        << "CWD FILTER: "
        << (
            selected_is_in_cwd_list
                ? "PASS"
                : "FAIL"
        )
        << '\n';

    std::cout
        << "RESUME: "
        << (
            resume_valid
                ? "PASS"
                : "FAIL"
        )
        << '\n';

    std::cout
        << "READ: "
        << (
            read_valid
                ? "PASS"
                : "FAIL"
        )
        << '\n';

    return 1;
}
