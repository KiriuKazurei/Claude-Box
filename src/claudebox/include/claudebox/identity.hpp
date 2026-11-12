#pragma once

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace claudebox::identity {

enum class FieldKind { Model, Identifier, Unknown };
enum class FieldMode { Expose, Hide, Override };

std::string normalize_key(std::string_view key);
std::string_view field_mode_name(FieldMode mode);
std::optional<FieldMode> field_mode_from_name(std::string_view name);

// Canonical field id for a raw inventory key, if known.
std::optional<std::string> canonical_field(std::string_view key);
FieldKind classify_key(std::string_view key);
std::vector<std::string> canonical_field_ids();
FieldKind kind_of_canonical(std::string_view canonical);

// True when a model-field *value* looks like a UUID or MAC (mis-labelled identifier).
std::optional<std::string> looks_like_identifier(std::string_view value);

struct FieldPolicy {
    FieldMode mode = FieldMode::Hide;
    std::string override_value;
};

struct Policy {
    // Canonical (or normalized unknown) field id -> policy.
    std::map<std::string, FieldPolicy> fields;

    static Policy defaults();
    static Policy parse_yaml(std::string_view text);
    static Policy load_file(const std::filesystem::path& path);

    std::string to_yaml() const;
    void save_file(const std::filesystem::path& path) const;

    FieldPolicy policy_for(std::string_view canonical) const;
};

// Default policy path under a box prefix.
std::filesystem::path policy_path(const std::filesystem::path& prefix);
void write_policy(const std::filesystem::path& path, const Policy& policy);

using Inventory = std::map<std::string, std::string>;

Inventory parse_flat_yaml(std::string_view text);
Inventory load_inventory_file(const std::filesystem::path& path);

// Preferred value for a canonical field, if present in the inventory.
std::optional<std::string> inventory_value(const Inventory& inventory,
                                           std::string_view canonical);

// Identifier values that must not leak when the field is hidden (or overridden).
std::vector<std::string> hidden_identifier_values(const Inventory& inventory,
                                                  const Policy& policy);

struct Presentation {
    // Canonical field id -> value that is allowed into the presentation layer.
    std::map<std::string, std::string> fields;
};

struct ApplyResult {
    bool ok = false;
    std::string error;
    Presentation presentation;
};

ApplyResult apply_policy(const Inventory& inventory, const Policy& policy);

void write_presentation(const std::filesystem::path& prefix,
                        const Presentation& presentation);

struct VerifyIssue {
    std::string message;
};

std::vector<VerifyIssue> verify(const Inventory& inventory,
                                const Policy& policy,
                                const Presentation& presentation);

std::vector<VerifyIssue> verify_prefix(const Inventory& inventory,
                                       const Policy& policy,
                                       const std::filesystem::path& prefix);

std::string yaml_quote(std::string_view s);
std::string shell_quote(std::string_view s);

}  // namespace claudebox::identity
