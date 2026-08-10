#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>

#include <pwd.h>
#include <unistd.h>

namespace {

bool environment_assignment(const std::string& value) {
    const std::size_t separator = value.find('=');

    if (separator == 0 || separator == std::string::npos) {
        return false;
    }

    for (std::size_t index = 0; index < separator; ++index) {
        const unsigned char character = value[index];

        if (
            !(character >= 'A' && character <= 'Z') &&
            !(character >= 'a' && character <= 'z') &&
            !(index > 0 && character >= '0' && character <= '9') &&
            character != '_'
        ) {
            return false;
        }
    }

    return true;
}

} // namespace

int main(int argc, char** argv) {
    if (::geteuid() != 0) {
        std::cerr << "ThreadDeck Shield executor must run as root.\n";
        return 126;
    }

    int command_index = 1;

    if (
        command_index < argc &&
        std::string(argv[command_index]) == "--"
    ) {
        ++command_index;
    }

    while (
        command_index < argc &&
        environment_assignment(argv[command_index])
    ) {
        const std::string assignment =
            argv[command_index];
        const std::size_t separator =
            assignment.find('=');

        if (
            ::setenv(
                assignment.substr(0, separator).c_str(),
                assignment.substr(separator + 1).c_str(),
                1) != 0
        ) {
            std::cerr
                << "Could not set privileged command environment: "
                << std::strerror(errno)
                << '\n';
            return 126;
        }

        ++command_index;
    }

    if (command_index >= argc) {
        std::cerr << "No privileged command was provided.\n";
        return 2;
    }

    const passwd* root_account = ::getpwuid(0);

    ::setenv("USER", "root", 1);
    ::setenv("LOGNAME", "root", 1);

    if (
        root_account != nullptr &&
        root_account->pw_dir != nullptr
    ) {
        ::setenv("HOME", root_account->pw_dir, 1);
    }

    ::execvp(
        argv[command_index],
        &argv[command_index]);

    std::cerr
        << "Could not execute privileged command: "
        << std::strerror(errno)
        << '\n';
    return errno == ENOENT ? 127 : 126;
}
