#pragma once

#include "claudebox/config.hpp"
#include "claudebox/identity.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace claudebox::settings {

struct Options {
    std::filesystem::path prefix;
    std::filesystem::path config;      // wsl-box.yaml (load + save)
    std::filesystem::path inventory;   // raw inventory (read only)
};

struct Paths {
    std::filesystem::path prefix;
    std::filesystem::path config;
    std::filesystem::path inventory;
    std::filesystem::path policy;
};

Paths resolve_paths(const Options& options);

struct SaveResult {
    bool ok = false;
    std::string error;
};

// In-memory settings document used by the Qt page and headless tests.
struct Session {
    claudebox::config::BoxConfig config;
    claudebox::identity::Inventory inventory;
    claudebox::identity::Policy policy;
    claudebox::identity::Presentation presentation;
    Paths paths;

    static Session load(const Options& options);

    // Write box config + per-field policy, then apply the presentation
    // layer into the prefix. Never writes the raw inventory. Always
    // persists hide_identifiers: true. Refuses to overwrite the in-repo
    // example config.
    SaveResult save();

    // Re-read config, policy and inventory from disk (same paths).
    void reload();

    void set_field_mode(std::string_view canonical,
                        claudebox::identity::FieldMode mode,
                        std::string override_value = {});
};

// Chinese labels for the settings page (and tests that inspect captions).
std::string field_label_zh(std::string_view canonical);
std::string field_kind_label_zh(claudebox::identity::FieldKind kind);
std::string field_mode_label_zh(claudebox::identity::FieldMode mode);

}  // namespace claudebox::settings
