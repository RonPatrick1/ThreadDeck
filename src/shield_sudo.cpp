#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include <unistd.h>

namespace {

bool root_identity(const std::string& value) {
    return
        value == "root" ||
        value == "0" ||
        value == "#0";
}

} // namespace

int main(int argc, char** argv) {
    const char* executor_value =
        ::getenv("THREADDECK_SHIELD_EXECUTOR");

    if (
        executor_value == nullptr ||
        *executor_value == '\0' ||
        executor_value[0] != '/'
    ) {
        std::cerr << "ThreadDeck Shield is not enabled.\n";
        return 1;
    }

    int command_index = 1;
    bool validate_only = false;

    while (command_index < argc) {
        const std::string option = argv[command_index];

        if (option == "--") {
            ++command_index;
            break;
        }

        if (
            option == "-n" ||
            option == "-E" ||
            option == "-H" ||
            option == "-S"
        ) {
            ++command_index;
            continue;
        }

        if (option == "-k" || option == "-K") {
            ++command_index;
            continue;
        }

        if (option == "-v") {
            validate_only = true;
            ++command_index;
            continue;
        }

        if (option == "-u" || option == "--user") {
            if (
                command_index + 1 >= argc ||
                !root_identity(argv[command_index + 1])
            ) {
                std::cerr
                    << "ThreadDeck Shield only supports the root identity.\n";
                return 2;
            }

            command_index += 2;
            continue;
        }

        if (option.rfind("--user=", 0) == 0) {
            if (!root_identity(option.substr(7))) {
                std::cerr
                    << "ThreadDeck Shield only supports the root identity.\n";
                return 2;
            }

            ++command_index;
            continue;
        }

        if (!option.empty() && option.front() == '-') {
            std::cerr
                << "Unsupported sudo option for ThreadDeck Shield: "
                << option
                << '\n';
            return 2;
        }

        break;
    }

    std::vector<std::string> arguments = {
        "/usr/bin/sudo",
        "-n",
        "--",
        executor_value,
        "--",
    };

    if (validate_only && command_index >= argc) {
        arguments.push_back("/usr/bin/true");
    } else {
        if (command_index >= argc) {
            std::cerr << "No sudo command was provided.\n";
            return 2;
        }

        for (; command_index < argc; ++command_index) {
            arguments.emplace_back(argv[command_index]);
        }
    }

    std::vector<char*> values;
    values.reserve(arguments.size() + 1);

    for (auto& argument : arguments) {
        values.push_back(argument.data());
    }

    values.push_back(nullptr);
    ::execv(values.front(), values.data());

    std::cerr
        << "Could not invoke ThreadDeck Shield: "
        << std::strerror(errno)
        << '\n';
    return 126;
}
