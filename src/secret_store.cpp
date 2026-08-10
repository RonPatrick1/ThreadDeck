#include "secret_store.h"

#include <dlfcn.h>
#include <glib.h>

#include <string>

namespace {

class SecretApi final {
public:
    using SchemaNew =
        void* (*)(const char*, int, ...);
    using SchemaUnref =
        void (*)(void*);
    using PasswordLookup =
        char* (*)(const void*, void*, GError**, ...);
    using PasswordStore =
        gboolean (*)(
            const void*,
            const char*,
            const char*,
            const char*,
            void*,
            GError**,
            ...);
    using PasswordClear =
        gboolean (*)(const void*, void*, GError**, ...);
    using PasswordFree =
        void (*)(char*);

    SecretApi() {
        library_ = dlopen(
            "libsecret-1.so.0",
            RTLD_NOW | RTLD_LOCAL);

        if (library_ == nullptr) {
            return;
        }

        schema_new = load<SchemaNew>("secret_schema_new");
        schema_unref = load<SchemaUnref>("secret_schema_unref");
        password_lookup =
            load<PasswordLookup>("secret_password_lookup_sync");
        password_store =
            load<PasswordStore>("secret_password_store_sync");
        password_clear =
            load<PasswordClear>("secret_password_clear_sync");
        password_free =
            load<PasswordFree>("secret_password_free");
    }

    ~SecretApi() {
        if (library_ != nullptr) {
            dlclose(library_);
        }
    }

    bool available() const {
        return
            library_ != nullptr &&
            schema_new != nullptr &&
            schema_unref != nullptr &&
            password_lookup != nullptr &&
            password_store != nullptr &&
            password_clear != nullptr &&
            password_free != nullptr;
    }

    std::string unavailable_error() const {
        return
            "Ubuntu Secret Service support is unavailable.";
    }

    SchemaNew schema_new{nullptr};
    SchemaUnref schema_unref{nullptr};
    PasswordLookup password_lookup{nullptr};
    PasswordStore password_store{nullptr};
    PasswordClear password_clear{nullptr};
    PasswordFree password_free{nullptr};

private:
    template<typename Function>
    Function load(const char* name) {
        return reinterpret_cast<Function>(
            dlsym(library_, name));
    }

    void* library_{nullptr};
};

class SplunkSchema final {
public:
    explicit SplunkSchema(SecretApi& api)
        : api_(api) {
        schema_ = api_.schema_new(
            "com.ronpatrick.ThreadDeck.Splunk",
            0,
            "account",
            0,
            nullptr);
    }

    ~SplunkSchema() {
        if (schema_ != nullptr) {
            api_.schema_unref(schema_);
        }
    }

    const void* get() const {
        return schema_;
    }

private:
    SecretApi& api_;
    void* schema_{nullptr};
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

    SecretApi api;

    if (!api.available()) {
        error = api.unavailable_error();
        return false;
    }

    SplunkSchema schema(api);

    if (schema.get() == nullptr) {
        error = "Could not create the Splunk keyring schema.";
        return false;
    }

    GError* keyring_error = nullptr;
    char* password = api.password_lookup(
        schema.get(),
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
        api.password_free(password);
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

    SecretApi api;

    if (!api.available()) {
        error = api.unavailable_error();
        return false;
    }

    SplunkSchema schema(api);

    if (schema.get() == nullptr) {
        error = "Could not create the Splunk keyring schema.";
        return false;
    }

    GError* keyring_error = nullptr;
    const gboolean stored = api.password_store(
        schema.get(),
        nullptr,
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

    SecretApi api;

    if (!api.available()) {
        error = api.unavailable_error();
        return false;
    }

    SplunkSchema schema(api);

    if (schema.get() == nullptr) {
        error = "Could not create the Splunk keyring schema.";
        return false;
    }

    GError* keyring_error = nullptr;
    const gboolean cleared = api.password_clear(
        schema.get(),
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
