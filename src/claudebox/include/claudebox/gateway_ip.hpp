#pragma once

#include "claudebox/config.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace claudebox::gateway {

// Host local address set (not loopback; not "public IP only"). Same idea as
// Get-LocalIpFingerprint in src/wsl-box/host/Watch-ClaudeBoxLocalIp.ps1:
// entries look like "iface|address/prefix" or a bare address, then sorted.
struct IpFingerprint {
    std::vector<std::string> entries;

    std::string text() const;
    bool operator==(const IpFingerprint&) const = default;
};

bool is_loopback_address(std::string_view address);

// Build a fingerprint from injected address strings. Loopback (127.0.0.0/8,
// ::1) is dropped. Tests use this so they never need a real Win11 NIC.
IpFingerprint make_fingerprint(const std::vector<std::string>& raw_entries);
bool fingerprint_changed(const IpFingerprint& previous, const IpFingerprint& current);

// Box Claude process names only: claude-desktop, claude, Claude.
// Does not match claudebox-*, gost, clash, or other unrelated names.
bool is_box_claude_process(std::string_view name);

struct BoxProcess {
    std::string name;
    int pid = 0;
    bool alive = true;
};

struct IpWatchConfig {
    bool armed = false;
    int grace_seconds = 10;
    int interval_seconds = 5;
    std::vector<std::string> errors;
    bool ok = true;
};

IpWatchConfig watch_config(const claudebox::config::BoxConfig& config);

enum class IpActionKind {
    NoOp,
    StopGateway,
    PoliteClose,
    WaitGrace,
    ForceKill,
};

enum class IpActionThread { None, Gateway, Process };

struct IpAction {
    IpActionKind kind = IpActionKind::NoOp;
    IpActionThread thread = IpActionThread::None;
    // Both StopGateway and PoliteClose use 0: they start in parallel as soon
    // as the IP set changes. Neither waits for the other. ForceKill uses the
    // process grace (default 10s). Gateway disconnect is never delayed to
    // the grace deadline.
    int start_offset_seconds = 0;
    int parallel_group = -1;  // 0 = the immediate dual start
    std::string label;
    std::vector<BoxProcess> targets;
};

struct IpChangePlan {
    bool ok = true;
    std::vector<std::string> errors;
    bool armed = false;
    bool fingerprint_changed = false;
    bool noop = true;
    std::string reason;
    int grace_seconds = 10;
    int interval_seconds = 5;
    IpFingerprint previous;
    IpFingerprint current;
    bool gateway_running = false;
    std::vector<BoxProcess> processes;
    std::vector<IpAction> actions;

    bool starts_gateway_disconnect() const;
    bool starts_polite_close() const;
    bool gateway_disconnect_delayed_until_grace() const;
};

struct IpChangeDryRun {
    bool ok = true;
    IpChangePlan plan;
    bool gateway_stopped = false;
    std::vector<BoxProcess> processes_after;
    std::vector<std::string> log;
};

// Plan the response to a (possibly injected) previous/current address set.
// Does not read a real NIC, start gost, or kill processes.
IpChangePlan plan_ip_change(const claudebox::config::BoxConfig& config,
                            const IpFingerprint& previous,
                            const IpFingerprint& current,
                            const std::vector<BoxProcess>& processes,
                            bool gateway_running = true);

// Dry-run the plan against a mock process list and a fake gateway flag.
// Stop-gateway and polite-close are recorded as starting together at t=0.
// After grace, remaining box Claude processes are marked force-killed.
// Never calls wsl.exe, never signals real processes, never starts/stops a
// live gost.
IpChangeDryRun dry_run_ip_change(const claudebox::config::BoxConfig& config,
                                 const IpFingerprint& previous,
                                 const IpFingerprint& current,
                                 const std::vector<BoxProcess>& processes,
                                 bool gateway_running = true);

std::string action_kind_name(IpActionKind kind);
std::string format_ip_change_plan(const IpChangePlan& plan,
                                  const IpChangeDryRun* run = nullptr);

}  // namespace claudebox::gateway
