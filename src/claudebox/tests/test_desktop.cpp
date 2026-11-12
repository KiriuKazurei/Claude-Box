#include "claudebox/config.hpp"
#include "claudebox/desktop.hpp"
#include "claudebox/spoof.hpp"

#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <unistd.h>

using claudebox::config::BoxConfig;
using claudebox::desktop::format_status;
using claudebox::desktop::plan;
using claudebox::desktop::start;
using claudebox::desktop::stop;
using claudebox::gateway::BoxProcess;

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
    if (env && *env) return fs::path(env);
    fs::path from_cmake(CLAUDEBOX_EXAMPLE_CONFIG);
    if (!from_cmake.empty()) return from_cmake;
    return fs::path("src/wsl-box/config/wsl-box.example.yaml");
}

static std::string read_text(const fs::path& path) {
    std::ifstream in(path);
    return std::string((std::istreambuf_iterator<char>(in)),
                       std::istreambuf_iterator<char>());
}

static void write_exec(const fs::path& path, const std::string& body) {
    fs::create_directories(path.parent_path());
    std::ofstream out(path);
    out << body;
    out.close();
    fs::permissions(path,
                    fs::perms::owner_all | fs::perms::group_read | fs::perms::group_exec |
                        fs::perms::others_read | fs::perms::others_exec,
                    fs::perm_options::replace);
}

static bool contains(const std::string& hay, const std::string& needle) {
    return hay.find(needle) != std::string::npos;
}

static fs::path unique_prefix(const std::string& tag) {
    const auto base = fs::temp_directory_path() / "claudebox-p10" /
                      (tag + "-" + std::to_string(::getpid()) + "-" +
                       std::to_string(static_cast<long>(std::time(nullptr))));
    fs::remove_all(base);
    fs::create_directories(base);
    return base;
}

static BoxConfig load_example() { return BoxConfig::load_file(example_config()); }

static void test_missing_binary_not_started() {
    unsetenv("CLAUDE_BOX_DESKTOP_CMD");
    const auto prefix = unique_prefix("missing");
    const auto cfg = load_example();
    const auto planned = plan(cfg, prefix);
    CHECK(!planned.binary_found);
    CHECK(!planned.would_start);
    CHECK(!planned.running);
    CHECK(planned.state == "missing-binary" || planned.state == "not-started");
    const auto status = format_status(planned);
    CHECK(contains(status, "未启动") || contains(status, "未找到"));
    CHECK(contains(status, "America/Los_Angeles"));
    CHECK(contains(status, "http://127.0.0.1:3080"));
    CHECK(!contains(status, "运行中") || contains(status, "未启动"));

    const auto started = start(cfg, prefix, true);
    CHECK(started.ok);
    CHECK(!started.started);
    CHECK(!started.plan.running);
    CHECK(contains(started.error, "not found") || contains(started.error, "Not started") ||
          contains(started.error, "not started"));
    // start applies spoof into the prefix even when the binary is missing
    CHECK(fs::exists(prefix / "env" / "spoof.env"));
    CHECK(contains(read_text(prefix / "env" / "spoof.env"), "America/Los_Angeles"));
    CHECK(contains(read_text(prefix / "env" / "spoof.env"), "SENTRY_NAME="));
    CHECK(contains(read_text(prefix / "env" / "session.env"), "HTTP_PROXY="));
    CHECK(contains(read_text(prefix / "env" / "session.env"), "http://127.0.0.1:3080"));
    fs::remove_all(prefix);
}

static void test_plan_with_mock_binary_still_dry_run() {
    const auto prefix = unique_prefix("mock-bin");
    auto cfg = load_example();
    const auto bin = prefix / "bin" / "claude-desktop";
    write_exec(bin, "#!/usr/bin/env bash\necho 'should-not-run'\nexit 0\n");
    const auto planned = plan(cfg, prefix);
    CHECK(planned.binary_found);
    CHECK(planned.would_start);
    CHECK(!planned.running);
    CHECK(planned.proxy_configured);
    CHECK(contains(planned.http_proxy, "http://127.0.0.1:3080"));
    CHECK(contains(planned.command_line, "--lang=en-US"));
    const auto started = start(cfg, prefix, true);
    CHECK(started.ok);
    CHECK(!started.started);
    CHECK(!started.plan.running);
    CHECK(started.plan.would_start);
    const auto status = format_status(started.plan);
    CHECK(contains(status, "未启动"));
    CHECK(contains(status, "已找到"));
    fs::remove_all(prefix);
}

static void test_missing_proxy_not_started() {
    const auto prefix = unique_prefix("no-proxy");
    auto cfg = load_example();
    cfg.gateway.upstream_http.clear();
    cfg.gateway.upstream_socks5.clear();
    write_exec(prefix / "bin" / "claude-desktop", "#!/usr/bin/env bash\nexit 0\n");
    const auto planned = plan(cfg, prefix);
    CHECK(planned.binary_found);
    CHECK(!planned.proxy_configured);
    CHECK(!planned.would_start);
    CHECK(!planned.running);
    const auto started = start(cfg, prefix, true);
    CHECK(!started.started);
    CHECK(!started.ok);
    CHECK(contains(started.error, "gateway") || contains(started.error, "proxy"));
    fs::remove_all(prefix);
}

static void test_stop_mock_pids() {
    const auto prefix = unique_prefix("stop");
    auto cfg = load_example();
    const std::vector<BoxProcess> procs = {
        {"claude-desktop", 101, true},
        {"claude", 102, true},
        {"firefox", 200, true},
        {"gost", 201, true},
    };
    const auto stopped = stop(cfg, procs, true);
    CHECK(stopped.ok);
    CHECK(stopped.dry_run);
    CHECK_EQ(stopped.targets.size(), size_t{2});
    bool firefox_alive = false;
    bool claude_alive = false;
    for (const auto& proc : stopped.processes_after) {
        if (proc.name == "firefox") firefox_alive = proc.alive;
        if (proc.name == "claude-desktop") claude_alive = proc.alive;
    }
    CHECK(firefox_alive);
    CHECK(!claude_alive);
    bool saw_polite = false, saw_force = false;
    for (const auto& line : stopped.log) {
        if (contains(line, "polite_close")) saw_polite = true;
        if (contains(line, "force_kill")) saw_force = true;
    }
    CHECK(saw_polite);
    CHECK(saw_force);
    const auto status = format_status(plan(cfg, prefix), &stopped);
    CHECK(contains(status, "polite_close"));
    CHECK(contains(status, "未启动"));
    fs::remove_all(prefix);
}

static void test_refuse_live_start() {
    const auto prefix = unique_prefix("live");
    auto cfg = load_example();
    write_exec(prefix / "bin" / "claude-desktop", "#!/usr/bin/env bash\nexit 0\n");
    const auto started = start(cfg, prefix, false);
    CHECK(!started.ok);
    CHECK(!started.started);
    CHECK(!started.plan.running);
    fs::remove_all(prefix);
}

int main() {
    try {
        test_missing_binary_not_started();
        test_plan_with_mock_binary_still_dry_run();
        test_missing_proxy_not_started();
        test_stop_mock_pids();
        test_refuse_live_start();
    } catch (const std::exception& ex) {
        std::cerr << "EXCEPTION " << ex.what() << "\n";
        return 1;
    }
    std::cout << "passed=" << g_passed << " failed=" << g_failed << "\n";
    return g_failed == 0 ? 0 : 1;
}
