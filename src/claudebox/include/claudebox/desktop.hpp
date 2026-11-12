#pragma once

#include "claudebox/config.hpp"
#include "claudebox/gateway_ip.hpp"

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace claudebox::desktop {

// Dry-run start/stop plan for Linux Claude Desktop. Never pretends a live
// Desktop window is running on this cloud computer. IP-change process kill
// stays in the unique-gateway module.
struct DesktopPlan {
    bool ok = false;
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
    std::filesystem::path prefix;
    std::filesystem::path binary;
    std::string command;
    std::vector<std::string> extra_args;
    std::vector<std::string> argv;
    std::string command_line;
    bool binary_found = false;
    bool proxy_configured = false;
    std::string http_proxy;
    std::string timezone;
    std::string locale;
    std::string electron_lang;
    std::filesystem::path session_env;
    std::filesystem::path spoof_env;
    std::filesystem::path host_model_env;
    bool spoof_env_present = false;
    bool host_model_present = false;
    // True only when a start could be attempted (binary + unique-gateway proxy).
    bool would_start = false;
    // Always false unless a real session is proven. Dry-run never sets this.
    bool running = false;
    // "not-started", "planned", "missing-binary", "missing-proxy".
    std::string state = "not-started";
};

struct StartResult {
    bool ok = false;
    // False on dry-run and when the binary is missing. Never means a window opened.
    bool started = false;
    std::string error;
    DesktopPlan plan;
};

struct StopResult {
    bool ok = false;
    bool dry_run = true;
    std::string error;
    std::vector<claudebox::gateway::BoxProcess> targets;
    std::vector<claudebox::gateway::BoxProcess> processes_after;
    std::vector<std::string> log;
    int grace_seconds = 10;
};

std::filesystem::path session_env_path(const std::filesystem::path& prefix);
std::filesystem::path host_model_env_path(const std::filesystem::path& prefix);

// Resolve a Desktop binary under the prefix or CLAUDE_BOX_DESKTOP_CMD.
// Does not treat a missing binary as a running session.
std::filesystem::path resolve_binary(const claudebox::config::BoxConfig& config,
                                     const std::filesystem::path& prefix);

DesktopPlan plan(const claudebox::config::BoxConfig& config,
                 const std::filesystem::path& prefix);

// Apply spoof artifacts (and use existing presentation / gateway proxy) then
// describe a start. With dry_run=true (the default), the Desktop binary is
// never executed and started stays false. A missing binary is reported as
// not-started, not as a successful session.
StartResult start(const claudebox::config::BoxConfig& config,
                  const std::filesystem::path& prefix,
                  bool dry_run = true);

// Dry-run polite close then force-kill against an injected process list.
// Never signals real PIDs, never calls wsl.exe.
StopResult stop(const claudebox::config::BoxConfig& config,
                const std::vector<claudebox::gateway::BoxProcess>& processes,
                bool dry_run = true);

// Chinese summary. States clearly when Desktop was not started.
std::string format_status(const DesktopPlan& plan, const StopResult* stop = nullptr);

}  // namespace claudebox::desktop
