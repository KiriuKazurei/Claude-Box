#include "claudebox/config.hpp"
#include "claudebox/gateway_ip.hpp"
#include "claudebox/settings.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

using claudebox::config::BoxConfig;
using claudebox::gateway::BoxProcess;
using claudebox::gateway::IpActionKind;
using claudebox::gateway::dry_run_ip_change;
using claudebox::gateway::fingerprint_changed;
using claudebox::gateway::format_ip_change_plan;
using claudebox::gateway::is_box_claude_process;
using claudebox::gateway::is_loopback_address;
using claudebox::gateway::make_fingerprint;
using claudebox::gateway::plan_ip_change;
using claudebox::gateway::watch_config;
using claudebox::settings::Session;

namespace fs = std::filesystem;

static int g_failed = 0;
static int g_passed = 0;

#define CHECK(cond)                                                             \
    do {                                                                        \
        if (cond) {                                                             \
            ++g_passed;                                                         \
        } else {                                                                \
            ++g_failed;                                                         \
            std::cerr << "FAIL " << __FILE__ << ":" << __LINE__ << "  " #cond   \
                      << "\n";                                                  \
        }                                                                       \
    } while (0)

#define CHECK_EQ(a, b)                                                          \
    do {                                                                        \
        const auto _a = (a);                                                    \
        const auto _b = (b);                                                    \
        if (_a == _b) {                                                         \
            ++g_passed;                                                         \
        } else {                                                                \
            ++g_failed;                                                         \
            std::cerr << "FAIL " << __FILE__ << ":" << __LINE__ << "  " #a      \
                      << " == " #b << "\n  left : " << _a << "\n  right: " << _b \
                      << "\n";                                                  \
        }                                                                       \
    } while (0)

#ifndef CLAUDEBOX_EXAMPLE_CONFIG
#define CLAUDEBOX_EXAMPLE_CONFIG ""
#endif

static fs::path example_config() {
    const char* env = std::getenv("CLAUDEBOX_EXAMPLE_CONFIG");
    if (env && *env) {
        return fs::path(env);
    }
    fs::path from_cmake(CLAUDEBOX_EXAMPLE_CONFIG);
    if (!from_cmake.empty()) {
        return from_cmake;
    }
    return fs::path("src/wsl-box/config/wsl-box.example.yaml");
}

static const auto kPrev = make_fingerprint({"Ethernet|192.168.1.20/24", "Ethernet|fe80::1/64"});
static const auto kCurr = make_fingerprint({"Ethernet|10.0.0.8/24", "Ethernet|fe80::1/64"});

static std::vector<BoxProcess> sample_procs() {
    return {
        {"claude-desktop", 101, true},
        {"claude", 102, true},
        {"Claude", 103, true},
        {"firefox", 200, true},
        {"gost", 201, true},
        {"claudebox-identity", 202, true},
        {"clash", 203, true},
    };
}

static void test_fingerprint_and_scope() {
    CHECK(is_loopback_address("127.0.0.1"));
    CHECK(is_loopback_address("127.1.2.3"));
    CHECK(is_loopback_address("lo|127.0.0.1/8"));
    CHECK(is_loopback_address("::1"));
    CHECK(is_loopback_address("lo|::1/128"));
    CHECK(!is_loopback_address("192.168.1.20"));
    CHECK(!is_loopback_address("Ethernet|10.0.0.8/24"));
    CHECK(!is_loopback_address("fe80::1"));

    const auto fp = make_fingerprint(
        {"Ethernet|192.168.1.20/24", "127.0.0.1", "lo|::1/128", "Ethernet|192.168.1.20/24",
         "Ethernet|fe80::1/64"});
    CHECK_EQ(fp.entries.size(), size_t{2});
    CHECK(!fingerprint_changed(fp, fp));
    CHECK(fingerprint_changed(kPrev, kCurr));

    CHECK(is_box_claude_process("claude-desktop"));
    CHECK(is_box_claude_process("/usr/bin/claude-desktop"));
    CHECK(is_box_claude_process("claude"));
    CHECK(is_box_claude_process("Claude"));
    CHECK(is_box_claude_process("Claude.exe"));
    CHECK(!is_box_claude_process("claudebox-identity"));
    CHECK(!is_box_claude_process("claude-box"));
    CHECK(!is_box_claude_process("firefox"));
    CHECK(!is_box_claude_process("gost"));
    CHECK(!is_box_claude_process("clash"));
    CHECK(!is_box_claude_process("sshd"));
}

static void test_default_off_is_noop() {
    const auto cfg = BoxConfig::load_file(example_config());
    CHECK(!cfg.features.kill_on_local_ip_change);
    CHECK_EQ(cfg.features.kill_grace_seconds, 10);
    CHECK_EQ(cfg.features.watch_interval_seconds, 5);

    const auto plan = plan_ip_change(cfg, kPrev, kCurr, sample_procs(), true);
    CHECK(plan.ok);
    CHECK(!plan.armed);
    CHECK(plan.fingerprint_changed);
    CHECK(plan.noop);
    CHECK(!plan.starts_gateway_disconnect());
    CHECK(!plan.starts_polite_close());
    CHECK_EQ(plan.grace_seconds, 10);

    const auto run = dry_run_ip_change(cfg, kPrev, kCurr, sample_procs(), true);
    CHECK(run.ok);
    CHECK(run.plan.noop);
    CHECK(!run.gateway_stopped);
    bool any_dead = false;
    for (const auto& proc : run.processes_after) {
        if (is_box_claude_process(proc.name) && !proc.alive) {
            any_dead = true;
        }
    }
    CHECK(!any_dead);
}

static void test_armed_unchanged_is_noop() {
    auto cfg = BoxConfig::defaults();
    cfg.features.kill_on_local_ip_change = true;
    const auto plan = plan_ip_change(cfg, kCurr, kCurr, sample_procs(), true);
    CHECK(plan.ok);
    CHECK(plan.armed);
    CHECK(!plan.fingerprint_changed);
    CHECK(plan.noop);
    CHECK(!plan.starts_gateway_disconnect());
    CHECK(!plan.starts_polite_close());

    const auto run = dry_run_ip_change(cfg, kCurr, kCurr, sample_procs(), true);
    CHECK(!run.gateway_stopped);
}

static void test_armed_change_starts_both_in_parallel() {
    auto cfg = BoxConfig::defaults();
    cfg.features.kill_on_local_ip_change = true;
    CHECK_EQ(cfg.features.kill_grace_seconds, 10);

    const auto plan = plan_ip_change(cfg, kPrev, kCurr, sample_procs(), true);
    CHECK(plan.ok);
    CHECK(plan.armed);
    CHECK(plan.fingerprint_changed);
    CHECK(!plan.noop);
    CHECK(plan.starts_gateway_disconnect());
    CHECK(plan.starts_polite_close());
    CHECK(!plan.gateway_disconnect_delayed_until_grace());
    CHECK_EQ(plan.grace_seconds, 10);

    bool saw_stop = false;
    bool saw_polite = false;
    bool saw_force = false;
    int stop_t = -1;
    int polite_t = -1;
    int force_t = -1;
    int stop_group = -2;
    int polite_group = -2;
    for (const auto& action : plan.actions) {
        if (action.kind == IpActionKind::StopGateway) {
            saw_stop = true;
            stop_t = action.start_offset_seconds;
            stop_group = action.parallel_group;
        }
        if (action.kind == IpActionKind::PoliteClose) {
            saw_polite = true;
            polite_t = action.start_offset_seconds;
            polite_group = action.parallel_group;
            CHECK_EQ(action.targets.size(), size_t{3});
            for (const auto& target : action.targets) {
                CHECK(is_box_claude_process(target.name));
            }
        }
        if (action.kind == IpActionKind::ForceKill) {
            saw_force = true;
            force_t = action.start_offset_seconds;
        }
        // Gateway stop must not be scheduled at or after the process grace.
        if (action.kind == IpActionKind::StopGateway) {
            CHECK(action.start_offset_seconds == 0);
            CHECK(action.start_offset_seconds < plan.grace_seconds);
        }
    }
    CHECK(saw_stop);
    CHECK(saw_polite);
    CHECK(saw_force);
    CHECK_EQ(stop_t, 0);
    CHECK_EQ(polite_t, 0);
    CHECK_EQ(stop_group, 0);
    CHECK_EQ(polite_group, 0);
    CHECK_EQ(force_t, 10);

    const auto run = dry_run_ip_change(cfg, kPrev, kCurr, sample_procs(), true);
    CHECK(run.ok);
    CHECK(run.gateway_stopped);
    const auto text = format_ip_change_plan(plan, &run);
    CHECK(text.find("并行") != std::string::npos);
    CHECK(text.find("先断开网关再礼貌关") != std::string::npos);  // denied wording, explained
    bool firefox_alive = false;
    bool identity_alive = false;
    int box_dead = 0;
    for (const auto& proc : run.processes_after) {
        if (proc.name == "firefox" || proc.name == "gost" || proc.name == "clash") {
            CHECK(proc.alive);
        }
        if (proc.name == "claudebox-identity") {
            identity_alive = proc.alive;
        }
        if (proc.name == "firefox") {
            firefox_alive = proc.alive;
        }
        if (is_box_claude_process(proc.name) && !proc.alive) {
            ++box_dead;
        }
    }
    CHECK(firefox_alive);
    CHECK(identity_alive);
    CHECK_EQ(box_dead, 3);

    // The dry-run log must show both threads starting at t=0, and must not
    // describe a serial "stop gateway then begin kill".
    int t0_gateway = 0;
    int t0_process = 0;
    for (const auto& line : run.log) {
        if (line.find("t=0 thread=gateway") != std::string::npos) {
            ++t0_gateway;
        }
        if (line.find("t=0 thread=process start") != std::string::npos) {
            ++t0_process;
        }
        CHECK(line.find("then polite") == std::string::npos);
        CHECK(line.find("after gateway") == std::string::npos);
    }
    CHECK_EQ(t0_gateway, 1);
    CHECK_EQ(t0_process, 1);
}

static void test_custom_grace() {
    auto cfg = BoxConfig::defaults();
    cfg.features.kill_on_local_ip_change = true;
    cfg.features.kill_grace_seconds = 3;
    const auto plan = plan_ip_change(cfg, kPrev, kCurr, sample_procs(), true);
    CHECK_EQ(plan.grace_seconds, 3);
    for (const auto& action : plan.actions) {
        if (action.kind == IpActionKind::ForceKill) {
            CHECK_EQ(action.start_offset_seconds, 3);
        }
        if (action.kind == IpActionKind::StopGateway) {
            CHECK_EQ(action.start_offset_seconds, 0);
        }
    }
}

static void test_rejects_bad_grace() {
    auto cfg = BoxConfig::defaults();
    cfg.features.kill_on_local_ip_change = true;
    cfg.features.kill_grace_seconds = -1;
    const auto plan = plan_ip_change(cfg, kPrev, kCurr, sample_procs(), true);
    CHECK(!plan.ok);
    CHECK(plan.noop);
}

int main() {
    try {
        test_fingerprint_and_scope();
        test_default_off_is_noop();
        test_armed_unchanged_is_noop();
        test_armed_change_starts_both_in_parallel();
        test_custom_grace();
        test_rejects_bad_grace();
    } catch (const std::exception& ex) {
        std::cerr << "EXCEPTION: " << ex.what() << "\n";
        return 1;
    }
    std::cout << "passed " << g_passed << "  failed " << g_failed << "\n";
    return g_failed == 0 ? 0 : 1;
}
