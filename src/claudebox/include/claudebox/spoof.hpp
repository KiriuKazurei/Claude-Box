#pragma once

#include "claudebox/config.hpp"

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace claudebox::spoof {

// Box-only spoof values. These are never taken from the host's real
// timezone or language, and they are not default presentable inventory
// fields.
struct SpoofValues {
    std::string timezone = "America/Los_Angeles";
    std::string locale = "en_US.UTF-8";
    std::string language = "en_US";
    std::string hostname = "claude-box";
    std::string electron_lang = "en-US";

    bool operator==(const SpoofValues&) const = default;
};

struct SpoofPlan {
    bool ok = false;
    std::vector<std::string> errors;
    SpoofValues values;
    std::filesystem::path prefix;
    std::filesystem::path spoof_env;      // <prefix>/env/spoof.env
    std::filesystem::path session_env;    // <prefix>/env/session.env
    std::filesystem::path etc_dir;        // <prefix>/etc
    std::filesystem::path localtime;      // <prefix>/etc/localtime
    std::filesystem::path timezone_file;  // <prefix>/etc/timezone
    std::filesystem::path zoneinfo;       // /usr/share/zoneinfo/<TZ> when present
    // Always false: this planner never writes the host /etc/localtime,
    // /etc/timezone, or /etc/profile.d, even when the process is root.
    bool writes_host_etc = false;
};

struct ApplyResult {
    bool ok = false;
    std::string error;
    SpoofPlan plan;
};

struct ProbeResult {
    bool ok = false;
    bool ran = false;
    std::string timezone;
    std::string locale;
    std::string source;  // "intl-node", "artifacts", or empty
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
};

struct VerifyResult {
    bool ok = false;
    std::vector<std::string> issues;
    ProbeResult probe;
    SpoofPlan plan;
};

SpoofValues from_config(const claudebox::config::IdentitySpoof& identity);

// Paths written under the prefix (same artifacts the linux dry-run uses:
// env/spoof.env, env/session.env, and a prefix-local etc/localtime).
std::filesystem::path spoof_env_path(const std::filesystem::path& prefix);
std::filesystem::path session_env_path(const std::filesystem::path& prefix);
std::filesystem::path prefix_localtime_path(const std::filesystem::path& prefix);
std::filesystem::path prefix_timezone_path(const std::filesystem::path& prefix);

// Normalize locale tags for Intl comparison: en_US.UTF-8 -> en-US.
std::string locale_language_tag(std::string_view locale);

// IANA timezone must exist under /usr/share/zoneinfo (when that tree is
// present) and must not contain path traversal.
bool timezone_allowed(std::string_view timezone, std::string* error = nullptr);
std::filesystem::path zoneinfo_path(std::string_view timezone);

SpoofPlan plan(const SpoofValues& values, const std::filesystem::path& prefix);

// Write spoof artifacts into the prefix. Never writes host /etc/localtime,
// host /etc/timezone, host TZ, or the host locale. Does not invoke
// apply-spoof.sh, because that script updates /etc when run as root.
ApplyResult apply(const SpoofValues& values, const std::filesystem::path& prefix);
ApplyResult apply_config(const claudebox::config::IdentitySpoof& identity,
                         const std::filesystem::path& prefix);

// Intl.DateTimeFormat-equivalent probe. Uses node when present, with the
// prefix spoof env (or the supplied values). Does not change the process
// timezone or the host locale.
ProbeResult probe_intl(const SpoofValues& values);

// Check prefix artifacts and the Intl probe against the configured fake
// values. The host's real timezone must not be required for success.
VerifyResult verify(const SpoofValues& values, const std::filesystem::path& prefix);

// Chinese summary for the settings page. States clearly these are box
// spoof values, not the host timezone/language.
std::string format_status(const SpoofPlan& plan, const VerifyResult* verify = nullptr);

}  // namespace claudebox::spoof
