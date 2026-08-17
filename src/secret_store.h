#pragma once

#include <string>

class SecretStore final {
public:
    static bool load_splunk_token(
        std::string& token,
        std::string& error);

    static bool save_splunk_token(
        const std::string& token,
        std::string& error);

    static bool clear_splunk_token(
        std::string& error);

    static bool load_remote_sudo_password(
        const std::string& host_identity,
        std::string& password,
        std::string& error);

    static bool save_remote_sudo_password(
        const std::string& host_identity,
        const std::string& password,
        std::string& error);

    static bool clear_remote_sudo_password(
        const std::string& host_identity,
        std::string& error);
};
