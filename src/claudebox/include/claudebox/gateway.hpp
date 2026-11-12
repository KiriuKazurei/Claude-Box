#pragma once

#include "claudebox/config.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace claudebox::gateway {

// Connection-log fields match ClaudeBox.Gateway / Write-ClaudeBoxGatewayLogEntry:
// ts, src, dst_ip, dst_port, sni_or_host, proto, action, upstream_ok, bytes
// and optional note. action = allow | deny | proxy.
struct LogEntry {
    std::string ts;
    std::string src;
    std::string dst_ip;
    int dst_port = 0;
    std::string sni_or_host;
    std::string proto = "tcp";
    std::string action = "proxy";
    std::optional<bool> upstream_ok;
    std::optional<std::int64_t> bytes;
    std::string note;
};

struct Validation {
    bool ok = false;
    std::vector<std::string> errors;
    bool http_configured = false;
    bool socks5_configured = false;
    bool upstream_configured = false;
    std::string primary;
    std::string upstream_url;
    std::string configured_host;
    std::string listen_host;
    int listen_port = 3080;
    bool logging_enabled = true;
    std::filesystem::path logging_path;
};

struct RelayPlan {
    bool ok = false;
    std::vector<std::string> errors;
    std::string tool = "gost";
    std::string status = "plan-only";
    std::string configured_host;
    std::string listen_host;
    int listen_port = 3080;
    std::string listen;
    std::string primary;
    std::string upstream_http;
    std::string upstream_socks5;
    std::string upstream_url;
    std::string forward;
    std::string command_line;
    std::string gost_yaml;
    std::filesystem::path gost_yaml_path;
    std::vector<std::string> blocklist;
    bool logging_enabled = true;
    std::filesystem::path logging_path;
};

struct LogSummary {
    bool file_exists = false;
    std::filesystem::path path;
    std::size_t total = 0;
    std::size_t deny_count = 0;
    std::size_t proxy_count = 0;
    std::size_t allow_count = 0;
    std::size_t skipped = 0;
    std::vector<LogEntry> recent;
};

struct GatewayStatus {
    bool upstream_configured = false;
    std::string configured_host;
    std::string listen_host;
    int listen_port = 3080;
    std::string planned_listen;
    std::string upstream_http;
    std::string upstream_socks5;
    std::string upstream_url;
    bool logging_enabled = true;
    std::filesystem::path logging_path;
    bool log_file_exists = false;
    std::string process_state;
    RelayPlan plan;
    LogSummary log;
    std::vector<std::string> errors;
};

// When logging.path is empty, the C++ core stores JSONL under the box prefix
// (var/logs/gateway.jsonl). This is not a host %LOCALAPPDATA% path, and it is
// not the PowerShell module's temp-dir fallback on Linux.
std::filesystem::path default_log_path(const std::filesystem::path& prefix);

// Resolve a configured path against the prefix. Empty paths, and unexpanded
// %VAR% paths when that environment variable is missing, use default_log_path.
std::filesystem::path resolve_log_path(std::string_view configured,
                                       const std::filesystem::path& prefix);

// Suffix match, same rules as Test-ClaudeBoxGatewayHostBlocked. Entries that
// include a path (a '/') are ignored here; use request_blocked for those.
bool host_blocked(std::string_view host, const std::vector<std::string>& blocklist);

// Host suffix match plus optional path-level rules. A blocklist entry that
// contains '/' (for example api.anthropic.com/api/event_logging, or a
// path-only /api/event_logging) matches only when both host and path agree.
// A host-only entry never blanket-denies every path on that host.
bool request_blocked(std::string_view host, std::string_view path,
                     const std::vector<std::string>& blocklist);

// At least one upstream is required. Does not start gost or bind a port.
Validation validate(const claudebox::config::BoxConfig& config,
                    const std::filesystem::path& prefix);

// Dry-run relay plan (gost -C <prefix>/var/gateway/gost.relay.yml).
// Encodes the blocklist as go-gost v3 bypass + hopGroup PathRegexp. Never starts a process.
RelayPlan plan_relay(const claudebox::config::BoxConfig& config,
                     const std::filesystem::path& prefix);

bool parse_log_line(std::string_view line, LogEntry* out);
std::string format_log_line(const LogEntry& entry);
// Append one JSONL line, creating parent directories. Fills ts when empty.
std::string append_log_entry(const std::filesystem::path& path, const LogEntry& entry);
LogSummary read_log(const std::filesystem::path& path, std::size_t last_n = 20);

GatewayStatus status(const claudebox::config::BoxConfig& config,
                     const std::filesystem::path& prefix,
                     std::size_t recent_n = 8);

// Chinese summary for the settings page. States clearly this is a dry-run.
std::string format_status(const GatewayStatus& st);

}  // namespace claudebox::gateway
