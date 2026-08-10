#include <cerrno>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <string>

#include <fcntl.h>
#include <pwd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {

bool parse_uid(const char* value, uid_t& uid) {
    if (value == nullptr || *value == '\0') {
        return false;
    }

    char* end = nullptr;
    errno = 0;
    const unsigned long parsed =
        std::strtoul(value, &end, 10);

    if (
        errno != 0 ||
        end == value ||
        *end != '\0' ||
        parsed > static_cast<unsigned long>(
            std::numeric_limits<uid_t>::max())
    ) {
        return false;
    }

    uid = static_cast<uid_t>(parsed);
    return true;
}

bool valid_user_name(const std::string& name) {
    if (name.empty()) {
        return false;
    }

    for (const unsigned char character : name) {
        if (
            !std::isalnum(character) &&
            character != '_' &&
            character != '-' &&
            character != '.'
        ) {
            return false;
        }
    }

    return true;
}

std::filesystem::path rule_path(uid_t uid) {
    return
        std::filesystem::path("/etc/sudoers.d") /
        ("threaddeck-shield-" +
         std::to_string(
             static_cast<unsigned long>(uid)));
}

std::string expected_rule(
    const std::string& user_name,
    uid_t uid,
    const std::filesystem::path& control_path,
    const std::filesystem::path& executor_path
) {
    return
        "# Managed by ThreadDeck Shield for UID " +
        std::to_string(
            static_cast<unsigned long>(uid)) +
        ". Disable Shield in ThreadDeck to remove this file.\n" +
        user_name +
        " ALL=(root) NOPASSWD: " +
        control_path.string() +
        ", " +
        executor_path.string() +
        "\n";
}

bool safe_executable_path(
    const std::filesystem::path& path
) {
    if (!path.is_absolute()) {
        return false;
    }

    const std::string value = path.string();

    for (const unsigned char character : value) {
        if (
            !std::isalnum(character) &&
            character != '/' &&
            character != '_' &&
            character != '-' &&
            character != '.'
        ) {
            return false;
        }
    }

    std::error_code error;
    return
        std::filesystem::is_regular_file(path, error) &&
        !error;
}

bool write_all(
    int descriptor,
    const std::string& contents
) {
    std::size_t written = 0;

    while (written < contents.size()) {
        const ssize_t result = ::write(
            descriptor,
            contents.data() + written,
            contents.size() - written);

        if (result < 0 && errno == EINTR) {
            continue;
        }

        if (result <= 0) {
            return false;
        }

        written += static_cast<std::size_t>(result);
    }

    return true;
}

bool validate_rule(
    const std::filesystem::path& path
) {
    const pid_t child = ::fork();

    if (child < 0) {
        return false;
    }

    if (child == 0) {
        ::execl(
            "/usr/sbin/visudo",
            "visudo",
            "-cf",
            path.c_str(),
            static_cast<char*>(nullptr));
        _exit(127);
    }

    int status = 0;

    while (::waitpid(child, &status, 0) < 0) {
        if (errno != EINTR) {
            return false;
        }
    }

    return
        WIFEXITED(status) &&
        WEXITSTATUS(status) == 0;
}

bool enable_rule(
    uid_t uid,
    const std::string& contents
) {
    const auto destination = rule_path(uid);
    const auto temporary =
        std::filesystem::path(
            destination.string() +
            ".tmp-" +
            std::to_string(
                static_cast<long long>(::getpid())));

    const int descriptor = ::open(
        temporary.c_str(),
        O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW,
        0440);

    if (descriptor < 0) {
        std::cerr
            << "Could not create temporary sudoers rule: "
            << std::strerror(errno)
            << '\n';
        return false;
    }

    bool success =
        ::fchmod(descriptor, 0440) == 0 &&
        write_all(descriptor, contents) &&
        ::fsync(descriptor) == 0;

    if (::close(descriptor) != 0) {
        success = false;
    }

    if (success) {
        success = validate_rule(temporary);
    }

    if (success) {
        success =
            ::rename(
                temporary.c_str(),
                destination.c_str()) == 0;
    }

    if (!success) {
        const int saved_error = errno;
        ::unlink(temporary.c_str());
        errno = saved_error;
        std::cerr
            << "Could not install a valid sudoers rule: "
            << std::strerror(errno)
            << '\n';
    }

    return success;
}

bool rule_is_enabled(
    uid_t uid,
    const std::string& contents
) {
    std::ifstream input(rule_path(uid));

    if (!input) {
        return false;
    }

    const std::string actual{
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>()};

    return actual == contents;
}

} // namespace

int main(int argc, char** argv) {
    if (
        argc != 4 ||
        (
            std::string(argv[1]) != "--enable" &&
            std::string(argv[1]) != "--disable" &&
            std::string(argv[1]) != "--status"
        )
    ) {
        std::cerr
            << "Usage: threaddeck-shield-control "
               "--enable|--disable|--status UID EXECUTOR\n";
        return 2;
    }

    if (::geteuid() != 0) {
        std::cerr << "Shield control must run as root.\n";
        return 1;
    }

    uid_t uid = 0;

    if (!parse_uid(argv[2], uid) || uid == 0) {
        std::cerr << "Invalid non-root UID.\n";
        return 2;
    }

    const passwd* account = ::getpwuid(uid);

    if (
        account == nullptr ||
        account->pw_name == nullptr ||
        !valid_user_name(account->pw_name)
    ) {
        std::cerr << "Could not resolve a safe Unix user name.\n";
        return 2;
    }

    std::error_code path_error;
    const auto control_path =
        std::filesystem::canonical(
            "/proc/self/exe",
            path_error);
    const auto executor_path =
        std::filesystem::weakly_canonical(
            argv[3],
            path_error);

    if (
        path_error ||
        !safe_executable_path(control_path) ||
        !safe_executable_path(executor_path)
    ) {
        std::cerr << "Unsafe Shield executable path.\n";
        return 2;
    }

    const std::string contents =
        expected_rule(
            account->pw_name,
            uid,
            control_path,
            executor_path);
    const std::string operation = argv[1];

    if (operation == "--status") {
        return rule_is_enabled(uid, contents) ? 0 : 1;
    }

    if (operation == "--disable") {
        const auto path = rule_path(uid);

        if (::unlink(path.c_str()) != 0 && errno != ENOENT) {
            std::cerr
                << "Could not remove Shield authorization: "
                << std::strerror(errno)
                << '\n';
            return 1;
        }

        return 0;
    }

    ::umask(0077);
    return enable_rule(uid, contents) ? 0 : 1;
}
