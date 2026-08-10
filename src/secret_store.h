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
};
