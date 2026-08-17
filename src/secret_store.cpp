#include "secret_store.h"

#include <libsecret/secret.h>

#include <string>

namespace {

const SecretSchema kSplunkSchema = {
    "com.ronpatrick.ThreadDeck.Splunk",
    SECRET_SCHEMA_NONE,
    {
        {
            "account",
            SECRET_SCHEMA_ATTRIBUTE_STRING,
        },
        {
            nullptr,
            SECRET_SCHEMA_ATTRIBUTE_STRING,
        },
    },
    0,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
};

const SecretSchema kRemoteSudoSchema = {
    "com.ronpatrick.ThreadDeck.RemoteSudo",
    SECRET_SCHEMA_NONE,
    {
        {
            "host",
            SECRET_SCHEMA_ATTRIBUTE_STRING,
        },
        {
            nullptr,
            SECRET_SCHEMA_ATTRIBUTE_STRING,
        },
    },
    0,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
};

std::string consume_error(GError* error) {
    if (error == nullptr) {
        return "Ubuntu Secret Service returned an unknown error.";
    }

    const std::string message =
        error->message != nullptr
            ? error->message
            : "Ubuntu Secret Service returned an unknown error.";

    g_error_free(error);
    return message;
}

}  // namespace

bool SecretStore::load_splunk_token(
    std::string& token,
    std::string& error) {
    token.clear();
    error.clear();

    GError* keyring_error = nullptr;
    char* password = secret_password_lookup_sync(
        &kSplunkSchema,
        nullptr,
        &keyring_error,
        "account",
        "default",
        nullptr);

    if (keyring_error != nullptr) {
        error = consume_error(keyring_error);
        return false;
    }

    if (password != nullptr) {
        token = password;
        secret_password_free(password);
    }

    return true;
}

bool SecretStore::save_splunk_token(
    const std::string& token,
    std::string& error) {
    error.clear();

    if (token.empty()) {
        error = "The Splunk token is empty.";
        return false;
    }

    GError* keyring_error = nullptr;
    const gboolean stored = secret_password_store_sync(
        &kSplunkSchema,
        SECRET_COLLECTION_DEFAULT,
        "ThreadDeck Splunk token",
        token.c_str(),
        nullptr,
        &keyring_error,
        "account",
        "default",
        nullptr);

    if (!stored) {
        error = consume_error(keyring_error);
        return false;
    }

    return true;
}

bool SecretStore::clear_splunk_token(
    std::string& error) {
    error.clear();

    GError* keyring_error = nullptr;
    const gboolean cleared = secret_password_clear_sync(
        &kSplunkSchema,
        nullptr,
        &keyring_error,
        "account",
        "default",
        nullptr);

    if (!cleared && keyring_error != nullptr) {
        error = consume_error(keyring_error);
        return false;
    }

    return true;
}

bool SecretStore::load_remote_sudo_password(
    const std::string& host_identity,
    std::string& password,
    std::string& error) {
    password.clear();
    error.clear();

    if (host_identity.empty()) {
        error = "The remote host identity is empty.";
        return false;
    }

    GError* keyring_error = nullptr;
    char* stored = secret_password_lookup_sync(
        &kRemoteSudoSchema,
        nullptr,
        &keyring_error,
        "host",
        host_identity.c_str(),
        nullptr);

    if (keyring_error != nullptr) {
        error = consume_error(keyring_error);
        return false;
    }

    if (stored != nullptr) {
        password = stored;
        secret_password_free(stored);
    }

    return true;
}

bool SecretStore::save_remote_sudo_password(
    const std::string& host_identity,
    const std::string& password,
    std::string& error) {
    error.clear();

    if (host_identity.empty()) {
        error = "The remote host identity is empty.";
        return false;
    }

    if (password.empty()) {
        error = "The remote sudo password is empty.";
        return false;
    }

    const std::string label =
        "ThreadDeck remote sudo password for " +
        host_identity;
    GError* keyring_error = nullptr;
    const gboolean stored = secret_password_store_sync(
        &kRemoteSudoSchema,
        SECRET_COLLECTION_DEFAULT,
        label.c_str(),
        password.c_str(),
        nullptr,
        &keyring_error,
        "host",
        host_identity.c_str(),
        nullptr);

    if (!stored) {
        error = consume_error(keyring_error);
        return false;
    }

    return true;
}

bool SecretStore::clear_remote_sudo_password(
    const std::string& host_identity,
    std::string& error) {
    error.clear();

    if (host_identity.empty()) {
        error = "The remote host identity is empty.";
        return false;
    }

    GError* keyring_error = nullptr;
    const gboolean cleared = secret_password_clear_sync(
        &kRemoteSudoSchema,
        nullptr,
        &keyring_error,
        "host",
        host_identity.c_str(),
        nullptr);

    if (!cleared && keyring_error != nullptr) {
        error = consume_error(keyring_error);
        return false;
    }

    return true;
}
