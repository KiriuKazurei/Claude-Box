#pragma once

#include "claudebox/config.hpp"
#include "claudebox/gateway_ip.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace claudebox::vault {

// Authenticated archive format: AES-256-GCM with PBKDF2-HMAC-SHA256
// (OpenSSL 3). Not binary-compatible with CCBaga .baga files.
inline constexpr const char* kCipherName = "aes-256-gcm";
inline constexpr const char* kKdfName = "pbkdf2-hmac-sha256";
inline constexpr int kKdfIterations = 200000;

struct Snapshot {
    std::string id;
    std::string label;
    std::string created_at;
    std::filesystem::path directory;
    std::filesystem::path archive;
    std::filesystem::path companion_profile;
    bool current = false;
    bool previous = false;
};

struct VaultPaths {
    std::filesystem::path prefix;
    std::filesystem::path vault_dir;
    std::filesystem::path workspace;
    std::filesystem::path generations;
    std::filesystem::path tmp;
    std::filesystem::path current_pointer;
    std::filesystem::path previous_pointer;
    std::filesystem::path companion_root;
};

struct ClosePlan {
    bool ok = true;
    std::vector<std::string> errors;
    int grace_seconds = 10;
    std::vector<claudebox::gateway::BoxProcess> targets;
    std::vector<claudebox::gateway::BoxProcess> skipped;
    bool starts_polite_close = false;
    bool force_after_grace = false;
    // Dry-run only on this computer: never signals real processes.
    bool dry_run = true;
};

struct VaultPlan {
    bool ok = false;
    std::vector<std::string> errors;
    bool feature_enabled = true;
    VaultPaths paths;
    std::string current_id;
    std::string previous_id;
    std::vector<Snapshot> snapshots;
    ClosePlan close;
};

struct ActionResult {
    bool ok = false;
    std::string error;
    VaultPlan plan;
    std::string current_id;
    std::string previous_id;
    std::vector<std::filesystem::path> written;
    // Always false: the vault never uses host Claude or browser profiles
    // as a working copy, and never writes host timezone/locale/proxy.
    bool wrote_outside_box = false;
    bool rolled_back = false;
    ClosePlan close;
};

// Test / operator hooks. force_fail_after is used to prove rollback.
struct ActionOptions {
    std::string label;
    bool dry_run_process_close = true;
    std::vector<claudebox::gateway::BoxProcess> processes;
    // "after_write", "after_verify", or empty.
    std::string force_fail_after;
};

std::filesystem::path default_vault_dir(const std::filesystem::path& prefix);
std::filesystem::path default_workspace(const std::filesystem::path& vault_dir);
std::filesystem::path default_companion_root(const std::filesystem::path& prefix);

// Host %APPDATA%\Claude, host Chrome/Edge profiles, and similar paths are
// not allowed as the vault or as a live workspace.
bool is_forbidden_host_path(const std::filesystem::path& path);
bool is_allowed_vault_dir(const std::filesystem::path& path,
                          const std::filesystem::path& prefix,
                          std::string* error = nullptr);

VaultPaths resolve_paths(const claudebox::config::BoxConfig& config,
                         const std::filesystem::path& prefix);

// Read a passphrase from CLAUDE_BOX_VAULT_PASSPHRASE_FILE (preferred) or
// CLAUDE_BOX_VAULT_PASSPHRASE. Never log the value.
bool load_passphrase(std::string* out, std::string* error);

// Box Claude plus a bound companion browser. Does not match host Chrome,
// gost, clash, or unrelated software.
bool is_vault_close_target(std::string_view name);
ClosePlan plan_close(const std::vector<claudebox::gateway::BoxProcess>& processes,
                     int grace_seconds = 10);

VaultPlan plan(const claudebox::config::BoxConfig& config,
               const std::filesystem::path& prefix,
               const ActionOptions& options = {});

std::vector<Snapshot> list_snapshots(const VaultPaths& paths);

// Snapshot the live workspace (and its bound companion profile under the
// prefix) into a new generation, then point current at it.
ActionResult archive(const claudebox::config::BoxConfig& config,
                     const std::filesystem::path& prefix,
                     std::string_view passphrase,
                     const ActionOptions& options = {});

// Special id "empty" switches to a fresh empty workspace. Any other id
// must be an existing generation. Current is archived first when dirty.
ActionResult switch_to(const claudebox::config::BoxConfig& config,
                       const std::filesystem::path& prefix,
                       std::string_view target_id,
                       std::string_view passphrase,
                       const ActionOptions& options = {});

// Restore the previous generation and workspace.
ActionResult rollback(const claudebox::config::BoxConfig& config,
                      const std::filesystem::path& prefix,
                      std::string_view passphrase,
                      const ActionOptions& options = {});

// Chinese summary for the settings page. Never includes a passphrase.
std::string format_status(const VaultPlan& plan);

}  // namespace claudebox::vault
