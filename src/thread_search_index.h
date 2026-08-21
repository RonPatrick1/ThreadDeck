#pragma once

#include <nlohmann/json.hpp>

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

struct sqlite3;

class ThreadSearchIndex final {
public:
    struct Match {
        nlohmann::json thread;
        std::string snippet;
    };

    ThreadSearchIndex() = default;
    ~ThreadSearchIndex();

    ThreadSearchIndex(const ThreadSearchIndex&) = delete;
    ThreadSearchIndex& operator=(const ThreadSearchIndex&) = delete;

    bool open(
        const std::filesystem::path& path,
        std::string& error);

    bool needs_refresh(
        const nlohmann::json& thread_summary,
        bool& refresh,
        std::string& error);

    bool replace_thread(
        const nlohmann::json& thread,
        std::string& error);

    bool search(
        const std::string& term,
        std::size_t limit,
        std::vector<Match>& matches,
        std::string& error);

private:
    static std::string revision_of(
        const nlohmann::json& thread);
    static std::string title_of(
        const nlohmann::json& thread);
    static std::string searchable_text_of(
        const nlohmann::json& thread);

    bool execute(
        const std::string& sql,
        std::string& error);

    sqlite3* database_{nullptr};
};
