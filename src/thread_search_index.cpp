#include "thread_search_index.h"

#include <sqlite3.h>

#include <algorithm>
#include <cctype>
#include <sstream>

namespace {

constexpr std::size_t kMaximumIndexedTextPerThread =
    8U * 1024U * 1024U;

void append_searchable_strings(
    const nlohmann::json& value,
    std::string& output,
    const std::string& key = {}) {
    if (output.size() >= kMaximumIndexedTextPerThread) {
        return;
    }

    if (value.is_string()) {
        if (
            key == "id" ||
            key == "type" ||
            key == "turnId" ||
            key == "threadId" ||
            key == "itemId"
        ) {
            return;
        }

        const std::string text = value.get<std::string>();
        if (text.empty()) {
            return;
        }

        if (!output.empty()) {
            output.push_back('\n');
        }

        const std::size_t remaining =
            kMaximumIndexedTextPerThread - output.size();
        output.append(text, 0, remaining);
        return;
    }

    if (value.is_array()) {
        for (const auto& entry : value) {
            append_searchable_strings(entry, output, key);
            if (output.size() >= kMaximumIndexedTextPerThread) {
                break;
            }
        }
        return;
    }

    if (value.is_object()) {
        for (auto entry = value.begin(); entry != value.end(); ++entry) {
            append_searchable_strings(
                entry.value(),
                output,
                entry.key());
            if (output.size() >= kMaximumIndexedTextPerThread) {
                break;
            }
        }
    }
}

std::string sqlite_error(sqlite3* database) {
    return database == nullptr
        ? std::string{"SQLite database is not open"}
        : std::string{sqlite3_errmsg(database)};
}

std::string lowercase(std::string value) {
    std::transform(
        value.begin(),
        value.end(),
        value.begin(),
        [](const unsigned char character) {
            return static_cast<char>(std::tolower(character));
        });
    return value;
}

std::string literal_snippet(
    const std::string& content,
    const std::string& term) {
    constexpr std::size_t context = 100;
    constexpr std::size_t maximum = 240;

    const std::string folded_content = lowercase(content);
    const std::size_t match =
        folded_content.find(lowercase(term));

    if (match == std::string::npos) {
        return {};
    }

    const std::size_t start =
        match > context ? match - context : 0;
    const std::size_t length = std::min(
        maximum,
        content.size() - start);
    std::string snippet = content.substr(start, length);
    std::replace(snippet.begin(), snippet.end(), '\n', ' ');
    std::replace(snippet.begin(), snippet.end(), '\r', ' ');
    return
        (start == 0 ? std::string{} : "… ") +
        snippet +
        (start + length >= content.size()
            ? std::string{}
            : " …");
}

}  // namespace

ThreadSearchIndex::~ThreadSearchIndex() {
    if (database_ != nullptr) {
        sqlite3_close(database_);
    }
}

bool ThreadSearchIndex::open(
    const std::filesystem::path& path,
    std::string& error) {
    if (database_ != nullptr) {
        return true;
    }

    std::error_code directory_error;
    std::filesystem::create_directories(
        path.parent_path(),
        directory_error);

    if (directory_error) {
        error =
            "Could not create the ThreadDeck search-index directory: " +
            directory_error.message();
        return false;
    }

    if (
        sqlite3_open_v2(
            path.c_str(),
            &database_,
            SQLITE_OPEN_READWRITE |
                SQLITE_OPEN_CREATE |
                SQLITE_OPEN_FULLMUTEX,
            nullptr) != SQLITE_OK
    ) {
        error = sqlite_error(database_);
        return false;
    }

    if (
        !execute("PRAGMA journal_mode=WAL;", error) ||
        !execute("PRAGMA synchronous=NORMAL;", error) ||
        !execute("PRAGMA max_page_count=131072;", error) ||
        !execute(
            "CREATE VIRTUAL TABLE IF NOT EXISTS thread_search "
            "USING fts5("
            "thread_id UNINDEXED, cwd UNINDEXED, title, content, "
            "revision UNINDEXED, metadata UNINDEXED, "
            "tokenize='trigram');",
            error)
    ) {
        return false;
    }

    return true;
}

bool ThreadSearchIndex::execute(
    const std::string& sql,
    std::string& error) {
    char* message = nullptr;
    const int result = sqlite3_exec(
        database_,
        sql.c_str(),
        nullptr,
        nullptr,
        &message);

    if (result == SQLITE_OK) {
        return true;
    }

    error = message == nullptr
        ? sqlite_error(database_)
        : std::string{message};
    sqlite3_free(message);
    return false;
}

std::string ThreadSearchIndex::revision_of(
    const nlohmann::json& thread) {
    for (const char* field : {"updatedAt", "createdAt"}) {
        if (thread.contains(field) && !thread[field].is_null()) {
            return thread[field].dump();
        }
    }

    return {};
}

std::string ThreadSearchIndex::title_of(
    const nlohmann::json& thread) {
    for (const char* field : {"name", "preview"}) {
        if (
            thread.contains(field) &&
            thread[field].is_string() &&
            !thread[field].get<std::string>().empty()
        ) {
            return thread[field].get<std::string>();
        }
    }

    return "Thread";
}

std::string ThreadSearchIndex::searchable_text_of(
    const nlohmann::json& thread) {
    std::string text;

    if (thread.contains("turns") && thread["turns"].is_array()) {
        append_searchable_strings(thread["turns"], text);
    }

    return text;
}

bool ThreadSearchIndex::needs_refresh(
    const nlohmann::json& thread_summary,
    bool& refresh,
    std::string& error) {
    refresh = true;

    const std::string thread_id =
        thread_summary.value("id", std::string{});
    if (thread_id.empty()) {
        error = "A thread summary has no ID";
        return false;
    }

    sqlite3_stmt* statement = nullptr;
    const char* sql =
        "SELECT revision FROM thread_search WHERE thread_id = ?1 LIMIT 1;";

    if (
        sqlite3_prepare_v2(
            database_,
            sql,
            -1,
            &statement,
            nullptr) != SQLITE_OK
    ) {
        error = sqlite_error(database_);
        return false;
    }

    sqlite3_bind_text(
        statement,
        1,
        thread_id.c_str(),
        -1,
        SQLITE_TRANSIENT);

    if (sqlite3_step(statement) == SQLITE_ROW) {
        const auto* stored = sqlite3_column_text(statement, 0);
        refresh =
            stored == nullptr ||
            revision_of(thread_summary) !=
                reinterpret_cast<const char*>(stored);
    }

    sqlite3_finalize(statement);
    return true;
}

bool ThreadSearchIndex::replace_thread(
    const nlohmann::json& thread,
    std::string& error) {
    const std::string thread_id =
        thread.value("id", std::string{});
    const std::string cwd =
        thread.value("cwd", std::string{});

    if (thread_id.empty()) {
        error = "A stored thread has no ID";
        return false;
    }

    if (!execute("BEGIN IMMEDIATE;", error)) {
        return false;
    }

    sqlite3_stmt* remove = nullptr;
    sqlite3_stmt* insert = nullptr;
    bool success = false;

    do {
        if (
            sqlite3_prepare_v2(
                database_,
                "DELETE FROM thread_search WHERE thread_id = ?1;",
                -1,
                &remove,
                nullptr) != SQLITE_OK
        ) {
            error = sqlite_error(database_);
            break;
        }

        sqlite3_bind_text(
            remove,
            1,
            thread_id.c_str(),
            -1,
            SQLITE_TRANSIENT);

        if (sqlite3_step(remove) != SQLITE_DONE) {
            error = sqlite_error(database_);
            break;
        }

        if (
            sqlite3_prepare_v2(
                database_,
                "INSERT INTO thread_search "
                "(thread_id, cwd, title, content, revision, metadata) "
                "VALUES (?1, ?2, ?3, ?4, ?5, ?6);",
                -1,
                &insert,
                nullptr) != SQLITE_OK
        ) {
            error = sqlite_error(database_);
            break;
        }

        const std::string title = title_of(thread);
        const std::string content = searchable_text_of(thread);
        const std::string revision = revision_of(thread);
        nlohmann::json summary = thread;
        summary.erase("turns");
        const std::string metadata = summary.dump();
        const std::string values[] = {
            thread_id,
            cwd,
            title,
            content,
            revision,
            metadata,
        };

        for (int index = 0; index < 6; ++index) {
            sqlite3_bind_text(
                insert,
                index + 1,
                values[index].c_str(),
                -1,
                SQLITE_TRANSIENT);
        }

        if (sqlite3_step(insert) != SQLITE_DONE) {
            error = sqlite_error(database_);
            break;
        }

        success = true;
    } while (false);

    sqlite3_finalize(remove);
    sqlite3_finalize(insert);

    std::string transaction_error;
    if (success) {
        success = execute("COMMIT;", transaction_error);
    } else {
        execute("ROLLBACK;", transaction_error);
    }

    if (!transaction_error.empty() && error.empty()) {
        error = transaction_error;
    }

    return success;
}

bool ThreadSearchIndex::search(
    const std::string& term,
    std::size_t limit,
    std::vector<Match>& matches,
    std::string& error) {
    matches.clear();

    if (term.empty() || limit == 0) {
        return true;
    }

    sqlite3_stmt* statement = nullptr;
    const bool short_query = term.size() < 3;
    const char* sql = short_query
        ? "SELECT metadata, content FROM thread_search "
          "WHERE lower(title) LIKE lower(?1) "
          "OR lower(content) LIKE lower(?1) "
          "ORDER BY CAST(revision AS REAL) DESC LIMIT ?2;"
        : "SELECT metadata, snippet(thread_search, 3, '', '', ' … ', 24) "
          "FROM thread_search WHERE thread_search MATCH ?1 "
          "ORDER BY bm25(thread_search), CAST(revision AS REAL) DESC "
          "LIMIT ?2;";

    if (
        sqlite3_prepare_v2(
            database_,
            sql,
            -1,
            &statement,
            nullptr) != SQLITE_OK
    ) {
        error = sqlite_error(database_);
        return false;
    }

    std::string query = term;
    if (short_query) {
        query = "%" + term + "%";
    } else {
        std::string escaped;
        escaped.reserve(term.size());
        for (const char character : term) {
            if (character == '"') {
                escaped += "\"\"";
            } else {
                escaped.push_back(character);
            }
        }
        query = "\"" + escaped + "\"";
    }

    sqlite3_bind_text(
        statement,
        1,
        query.c_str(),
        -1,
        SQLITE_TRANSIENT);
    sqlite3_bind_int64(
        statement,
        2,
        static_cast<sqlite3_int64>(limit));

    int step_result = SQLITE_OK;
    while ((step_result = sqlite3_step(statement)) == SQLITE_ROW) {
        const auto* metadata = sqlite3_column_text(statement, 0);
        const auto* snippet = sqlite3_column_text(statement, 1);
        if (metadata == nullptr) {
            continue;
        }

        try {
            Match match;
            match.thread = nlohmann::json::parse(
                reinterpret_cast<const char*>(metadata));
            match.snippet = snippet == nullptr
                ? std::string{}
                : reinterpret_cast<const char*>(snippet);

            if (short_query) {
                match.snippet = literal_snippet(
                    match.snippet,
                    term);
            }
            matches.push_back(std::move(match));
        } catch (const nlohmann::json::exception&) {
            continue;
        }
    }

    if (step_result != SQLITE_DONE) {
        error = sqlite_error(database_);
        sqlite3_finalize(statement);
        return false;
    }

    sqlite3_finalize(statement);
    return true;
}
