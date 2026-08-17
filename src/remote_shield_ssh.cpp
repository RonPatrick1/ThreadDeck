#include "secret_store.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cctype>
#include <csignal>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include <poll.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {

constexpr const char* kSystemSsh = "/usr/bin/ssh";

bool option_requires_argument(const std::string& option) {
    if (option.size() != 2 || option.front() != '-') {
        return false;
    }

    const std::string options_with_arguments =
        "BbcDEeFIiJLlmOoPpQRSWw";
    return
        options_with_arguments.find(option[1]) !=
        std::string::npos;
}

struct SshInvocation {
    std::size_t destination_index{0};
    std::size_t command_index{0};
    std::string destination;
    bool valid{false};
};

SshInvocation parse_invocation(int argc, char** argv) {
    SshInvocation invocation;
    bool options_finished = false;

    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];

        if (!options_finished && argument == "--") {
            options_finished = true;
            continue;
        }

        if (
            !options_finished &&
            argument.size() > 1 &&
            argument.front() == '-'
        ) {
            if (
                option_requires_argument(argument) &&
                index + 1 < argc
            ) {
                ++index;
            }
            continue;
        }

        invocation.destination_index =
            static_cast<std::size_t>(index);
        invocation.command_index =
            static_cast<std::size_t>(index + 1);
        invocation.destination = argument;
        invocation.valid = true;
        return invocation;
    }

    return invocation;
}

std::string lower_ascii(std::string value) {
    std::transform(
        value.begin(),
        value.end(),
        value.begin(),
        [](unsigned char character) {
            return static_cast<char>(
                std::tolower(character));
        });
    return value;
}

std::string host_part(const std::string& destination) {
    const std::size_t separator = destination.rfind('@');
    return lower_ascii(
        separator == std::string::npos
            ? destination
            : destination.substr(separator + 1));
}

std::string configured_secret_identity(
    const std::string& destination
) {
    const char* configured =
        ::getenv("THREADDECK_REMOTE_SHIELD_HOSTS");

    if (configured == nullptr || *configured == '\0') {
        return {};
    }

    try {
        const nlohmann::json hosts =
            nlohmann::json::parse(configured);

        if (!hosts.is_object()) {
            return {};
        }

        const auto exact = hosts.find(destination);

        if (exact != hosts.end() && exact->is_string()) {
            return exact->get<std::string>();
        }

        const std::string destination_host =
            host_part(destination);

        for (const auto& entry : hosts.items()) {
            if (
                entry.value().is_string() &&
                host_part(entry.key()) == destination_host
            ) {
                return entry.value().get<std::string>();
            }
        }
    } catch (const std::exception&) {
        return {};
    }

    return {};
}

bool sudo_program(const std::string& value) {
    return
        value == "sudo" ||
        value == "/usr/bin/sudo" ||
        value == "/bin/sudo";
}

bool starts_with_sudo_command(const std::string& value) {
    for (const std::string& prefix : {
             std::string{"sudo"},
             std::string{"/usr/bin/sudo"},
             std::string{"/bin/sudo"},
         }) {
        if (
            value == prefix ||
            (
                value.size() > prefix.size() &&
                value.rfind(prefix, 0) == 0 &&
                std::isspace(
                    static_cast<unsigned char>(
                        value[prefix.size()]))
            )
        ) {
            return true;
        }
    }

    return false;
}

std::string shell_quote(const std::string& value) {
    std::string quoted{"'"};

    for (const char character : value) {
        if (character == '\'') {
            quoted += "'\\''";
        } else {
            quoted += character;
        }
    }

    quoted += '\'';
    return quoted;
}

std::string rewritten_sudo_command(
    int argc,
    char** argv,
    std::size_t command_index
) {
    if (command_index >= static_cast<std::size_t>(argc)) {
        return {};
    }

    const std::string first = argv[command_index];
    const std::string prefix =
        "sudo -k >/dev/null 2>&1; exec sudo -S -p ''";

    if (sudo_program(first)) {
        std::string rewritten = prefix;

        for (
            std::size_t index = command_index + 1;
            index < static_cast<std::size_t>(argc);
            ++index
        ) {
            const std::string argument = argv[index];

            if (argument == "-n" || argument == "-S") {
                continue;
            }

            if (argument == "-p") {
                if (index + 1 < static_cast<std::size_t>(argc)) {
                    ++index;
                }
                continue;
            }

            if (argument.rfind("--prompt=", 0) == 0) {
                continue;
            }

            rewritten += " " + shell_quote(argument);
        }

        return rewritten;
    }

    if (!starts_with_sudo_command(first)) {
        return {};
    }

    std::size_t remainder = first.find_first_of(" \t");
    std::string command =
        remainder == std::string::npos
            ? std::string{}
            : first.substr(remainder);

    while (
        !command.empty() &&
        std::isspace(
            static_cast<unsigned char>(command.front()))
    ) {
        command.erase(command.begin());
    }

    if (command.rfind("-n", 0) == 0) {
        if (
            command.size() == 2 ||
            std::isspace(
                static_cast<unsigned char>(command[2]))
        ) {
            command.erase(0, 2);
        }
    }

    std::string rewritten = prefix;

    if (!command.empty()) {
        rewritten += " " + command;
    }

    for (
        std::size_t index = command_index + 1;
        index < static_cast<std::size_t>(argc);
        ++index
    ) {
        rewritten += " " + shell_quote(argv[index]);
    }

    return rewritten;
}

int exec_system_ssh(int argc, char** argv) {
    std::vector<char*> arguments;
    arguments.reserve(static_cast<std::size_t>(argc) + 1);
    arguments.push_back(const_cast<char*>(kSystemSsh));

    for (int index = 1; index < argc; ++index) {
        arguments.push_back(argv[index]);
    }

    arguments.push_back(nullptr);
    ::execv(kSystemSsh, arguments.data());
    std::cerr
        << "Could not execute system ssh: "
        << std::strerror(errno)
        << '\n';
    return errno == ENOENT ? 127 : 126;
}

bool write_all(int descriptor, const std::string& value) {
    std::size_t offset = 0;

    while (offset < value.size()) {
        const ssize_t written = ::write(
            descriptor,
            value.data() + offset,
            value.size() - offset);

        if (written < 0 && errno == EINTR) {
            continue;
        }

        if (written <= 0) {
            return false;
        }

        offset += static_cast<std::size_t>(written);
    }

    return true;
}

int run_remote_shield_ssh(
    int argc,
    char** argv,
    const SshInvocation& invocation,
    const std::string& remote_command,
    std::string password
) {
    int child_input[2]{};

    if (::pipe(child_input) != 0) {
        std::cerr
            << "Could not create the Remote Shield input pipe: "
            << std::strerror(errno)
            << '\n';
        return 126;
    }

    const pid_t child = ::fork();

    if (child < 0) {
        ::close(child_input[0]);
        ::close(child_input[1]);
        std::cerr
            << "Could not start ssh for Remote Shield: "
            << std::strerror(errno)
            << '\n';
        return 126;
    }

    if (child == 0) {
        ::dup2(child_input[0], STDIN_FILENO);
        ::close(child_input[0]);
        ::close(child_input[1]);

        std::vector<std::string> owned;
        owned.reserve(static_cast<std::size_t>(argc) + 5);
        owned.push_back(kSystemSsh);

        for (
            std::size_t index = 1;
            index < invocation.destination_index;
            ++index
        ) {
            owned.emplace_back(argv[index]);
        }

        owned.emplace_back("-o");
        owned.emplace_back("BatchMode=yes");
        owned.emplace_back(argv[invocation.destination_index]);
        owned.push_back(remote_command);

        std::vector<char*> values;
        values.reserve(owned.size() + 1);

        for (auto& value : owned) {
            values.push_back(value.data());
        }

        values.push_back(nullptr);
        ::execv(kSystemSsh, values.data());
        _exit(errno == ENOENT ? 127 : 126);
    }

    ::close(child_input[0]);
    ::signal(SIGPIPE, SIG_IGN);

    const bool password_written =
        write_all(child_input[1], password + "\n");
    std::fill(password.begin(), password.end(), '\0');
    password.clear();

    if (!password_written) {
        ::close(child_input[1]);
    }

    int status = 0;
    bool input_open = password_written;

    while (true) {
        const pid_t waited =
            ::waitpid(child, &status, WNOHANG);

        if (waited == child) {
            break;
        }

        if (waited < 0 && errno != EINTR) {
            status = 126 << 8;
            break;
        }

        if (!input_open) {
            std::this_thread::sleep_for(
                std::chrono::milliseconds(50));
            continue;
        }

        pollfd input_poll{};
        input_poll.fd = STDIN_FILENO;
        input_poll.events = POLLIN | POLLHUP;
        const int ready = ::poll(&input_poll, 1, 50);

        if (ready <= 0) {
            continue;
        }

        char buffer[4096];
        const ssize_t count = ::read(
            STDIN_FILENO,
            buffer,
            sizeof(buffer));

        if (count > 0) {
            if (
                !write_all(
                    child_input[1],
                    std::string(
                        buffer,
                        static_cast<std::size_t>(count)))
            ) {
                ::close(child_input[1]);
                input_open = false;
            }
        } else {
            ::close(child_input[1]);
            input_open = false;
        }
    }

    if (input_open) {
        ::close(child_input[1]);
    }

    return WIFEXITED(status)
        ? WEXITSTATUS(status)
        : 126;
}

} // namespace

int main(int argc, char** argv) {
    const SshInvocation invocation =
        parse_invocation(argc, argv);

    if (
        !invocation.valid ||
        invocation.command_index >=
            static_cast<std::size_t>(argc)
    ) {
        return exec_system_ssh(argc, argv);
    }

    const std::string secret_identity =
        configured_secret_identity(
            invocation.destination);

    if (secret_identity.empty()) {
        return exec_system_ssh(argc, argv);
    }

    const std::string remote_command =
        rewritten_sudo_command(
            argc,
            argv,
            invocation.command_index);

    if (remote_command.empty()) {
        return exec_system_ssh(argc, argv);
    }

    std::string password;
    std::string error;

    if (
        !SecretStore::load_remote_sudo_password(
            secret_identity,
            password,
            error) ||
        password.empty()
    ) {
        std::cerr
            << "ThreadDeck Remote Shield has no usable sudo credential for "
            << invocation.destination
            << "."
            << (
                error.empty()
                    ? std::string{}
                    : " " + error
            )
            << '\n';
        return 1;
    }

    if (
        password.find('\n') != std::string::npos ||
        password.find('\r') != std::string::npos
    ) {
        std::fill(password.begin(), password.end(), '\0');
        std::cerr
            << "ThreadDeck Remote Shield cannot use a credential containing a newline.\n";
        return 1;
    }

    return run_remote_shield_ssh(
        argc,
        argv,
        invocation,
        remote_command,
        std::move(password));
}
