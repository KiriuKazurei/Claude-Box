#include "claudebox/config.hpp"
#include "claudebox/gateway.hpp"
#include "claudebox/settings.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

using claudebox::config::BoxConfig;
using claudebox::gateway::LogEntry;
using claudebox::gateway::append_log_entry;
using claudebox::gateway::default_log_path;
using claudebox::gateway::format_status;
using claudebox::gateway::host_blocked;
using claudebox::gateway::parse_log_line;
using claudebox::gateway::plan_relay;
using claudebox::gateway::read_log;
using claudebox::gateway::request_blocked;
using claudebox::gateway::resolve_log_path;
using claudebox::gateway::status;
using claudebox::gateway::validate;
using claudebox::settings::Options;
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
#ifndef CLAUDEBOX_GATEWAY_SAMPLE
#define CLAUDEBOX_GATEWAY_SAMPLE ""
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

static fs::path sample_log() {
    const char* env = std::getenv("CLAUDEBOX_GATEWAY_SAMPLE");
    if (env && *env) {
        return fs::path(env);
    }
    fs::path from_cmake(CLAUDEBOX_GATEWAY_SAMPLE);
    if (!from_cmake.empty()) {
        return from_cmake;
    }
    return fs::path("src/ClaudeBox.Gateway/samples/gateway.log.example.jsonl");
}

static std::string read_text(const fs::path& path) {
    std::ifstream in(path);
    return std::string((std::istreambuf_iterator<char>(in)),
                       std::istreambuf_iterator<char>());
}

static bool contains(const std::string& hay, const std::string& needle) {
    return hay.find(needle) != std::string::npos;
}

static fs::path unique_prefix(const char* tag) {
    auto dir = fs::temp_directory_path() / "claudebox-p6" / tag;
    fs::remove_all(dir);
    fs::create_directories(dir);
    return dir;
}

static void test_default_log_path_under_prefix() {
    const auto prefix = unique_prefix("log-path");
    const auto def = default_log_path(prefix);
    CHECK_EQ(def, prefix / "var" / "logs" / "gateway.jsonl");
    CHECK(!contains(def.string(), "LOCALAPPDATA"));
    CHECK(!contains(def.string(), "LocalAppData"));
    CHECK(contains(def.string(), "/var/logs/gateway.jsonl"));

    const auto empty = resolve_log_path("", prefix);
    CHECK_EQ(empty, def);
    CHECK_EQ(resolve_log_path("   ", prefix), def);

    const auto unexpanded = resolve_log_path(
        "%LOCALAPPDATA%\\ClaudeBox\\logs\\wsl-gateway.jsonl", prefix);
    // On this Linux box LOCALAPPDATA is typically unset, so we must not keep
    // a host LocalAppData path.
    CHECK_EQ(unexpanded, def);
    CHECK(!contains(unexpanded.string(), "LOCALAPPDATA"));

    const auto absolute = resolve_log_path("/tmp/custom-gw.jsonl", prefix);
    CHECK_EQ(absolute, fs::path("/tmp/custom-gw.jsonl"));

    const auto relative = resolve_log_path("logs/custom.jsonl", prefix);
    CHECK_EQ(relative, prefix / "logs" / "custom.jsonl");
}

static void test_blocklist_matches_powershell_suffix() {
    const auto cfg = BoxConfig::load_file(example_config());
    const auto& list = cfg.gateway.blocklist;
    CHECK(list.size() >= 11);
    CHECK_EQ(list[0], std::string("sentry.io"));

    // Suffix match used by Test-ClaudeBoxGatewayHostBlocked (host-only entries).
    CHECK(host_blocked("sentry.io", list));
    CHECK(host_blocked("ingest.us.sentry.io", list));
    CHECK(host_blocked("o1158394.ingest.us.sentry.io", list));
    CHECK(host_blocked("O1158394.Ingest.US.Sentry.IO.", list));
    CHECK(host_blocked("browser-intake-datadoghq.com", list));
    CHECK(host_blocked("browser-intake-us3-datadoghq.com", list));
    CHECK(host_blocked("browser-intake-us5-datadoghq.com", list));
    CHECK(host_blocked("browser-intake-ap1-datadoghq.com", list));
    CHECK(host_blocked("browser-intake-ap2-datadoghq.com", list));
    CHECK(host_blocked("browser-intake-datadoghq.eu", list));
    CHECK(host_blocked("browser-intake-ddog-gov.com", list));
    CHECK(!host_blocked("datadoghq.com", list));
    CHECK(!host_blocked("api.anthropic.com", list));
    CHECK(!host_blocked("", list));

    // Path rules block event_logging and /api/v2/rum. The main API stays open.
    // Path-boundary: /api/event_logging2 and /api/v2/rumble are not blocked.
    CHECK(!request_blocked("api.anthropic.com", "/v1/messages", list));
    CHECK(request_blocked("api.anthropic.com", "/api/event_logging", list));
    CHECK(request_blocked("api.anthropic.com", "/api/event_logging/", list));
    CHECK(request_blocked("api.anthropic.com", "/api/event_logging?x=1", list));
    CHECK(request_blocked("api.anthropic.com", "/api/event_logging/v2/batch", list));
    CHECK(!request_blocked("api.anthropic.com", "/api/event_logging2", list));
    CHECK(request_blocked("api.anthropic.com", "/api/v2/rum", list));
    CHECK(request_blocked("claude.ai", "/api/v2/rum", list));
    CHECK(!request_blocked("api.anthropic.com", "/api/v2/rumble", list));
    CHECK(!host_blocked("api.anthropic.com", list));

    const std::vector<std::string> path_only = {"/api/v2/rum"};
    CHECK(request_blocked("browser-intake-datadoghq.com", "/api/v2/rum", path_only));
    CHECK(!request_blocked("browser-intake-datadoghq.com", "/api/v2/rumble", path_only));
}

static void test_plan_and_validate() {
    const auto prefix = unique_prefix("plan");
    auto cfg = BoxConfig::load_file(example_config());
    const auto plan = plan_relay(cfg, prefix);
    CHECK(plan.ok);
    CHECK_EQ(plan.tool, std::string("gost"));
    CHECK_EQ(plan.status, std::string("plan-only"));
    CHECK_EQ(plan.listen_host, std::string("0.0.0.0"));
    CHECK_EQ(plan.listen_port, 3080);
    CHECK_EQ(plan.listen, std::string("http://0.0.0.0:3080"));
    CHECK_EQ(plan.upstream_http, std::string("http://127.0.0.1:7890"));
    CHECK_EQ(plan.upstream_socks5, std::string("socks5://127.0.0.1:7891"));
    CHECK_EQ(plan.upstream_url, std::string("http://127.0.0.1:7890"));
    CHECK(plan.command_line.find("gost -C ") == 0);
    CHECK(contains(plan.command_line, "gost.relay.yml"));
    CHECK_EQ(plan.gost_yaml_path, prefix / "var" / "gateway" / "gost.relay.yml");
    CHECK(std::filesystem::is_regular_file(plan.gost_yaml_path));
    CHECK(contains(plan.gost_yaml, "127.0.0.1:7890"));
    CHECK(contains(plan.gost_yaml, "0.0.0.0:3080"));
    CHECK(contains(plan.gost_yaml, "bypass-telemetry-hosts"));
    CHECK(contains(plan.gost_yaml, "sentry.io"));
    CHECK(contains(plan.gost_yaml, "*.sentry.io"));
    CHECK(contains(plan.gost_yaml, "browser-intake-us3-datadoghq.com"));
    CHECK(contains(plan.gost_yaml, "browser-intake-ap1-datadoghq.com"));
    CHECK(contains(plan.gost_yaml, "browser-intake-ap2-datadoghq.com"));
    CHECK(contains(plan.gost_yaml, "browser-intake-datadoghq.eu"));
    CHECK(contains(plan.gost_yaml, "browser-intake-ddog-gov.com"));
    CHECK(contains(plan.gost_yaml, "PathRegexp"));
    CHECK(contains(plan.gost_yaml, "/api/event_logging"));
    CHECK(contains(plan.gost_yaml, "/api/v2/rum"));
    CHECK(contains(plan.gost_yaml, "hopGroup"));
    // Must not blanket-deny the main Anthropic API as a host bypass matcher.
    CHECK(!contains(plan.gost_yaml, "\"api.anthropic.com\""));
    CHECK(!contains(plan.gost_yaml, "\"*.api.anthropic.com\""));
    CHECK_EQ(plan.logging_path, default_log_path(prefix));
    CHECK(plan.logging_enabled);

    cfg.gateway.host = "172.20.0.1";
    cfg.gateway.http_port = 4080;
    cfg.gateway.upstream_http.clear();
    const auto socks_plan = plan_relay(cfg, prefix);
    CHECK(socks_plan.ok);
    CHECK_EQ(socks_plan.listen, std::string("http://172.20.0.1:4080"));
    CHECK_EQ(socks_plan.upstream_url, std::string("socks5://127.0.0.1:7891"));
    CHECK(socks_plan.command_line.find("gost -C ") == 0);
    CHECK(contains(socks_plan.gost_yaml, "172.20.0.1:4080"));
    CHECK(contains(socks_plan.gost_yaml, "socks5://127.0.0.1:7891"));

    auto bad = cfg;
    bad.gateway.upstream_http.clear();
    bad.gateway.upstream_socks5.clear();
    const auto bad_plan = plan_relay(bad, prefix);
    CHECK(!bad_plan.ok);
    CHECK(!bad_plan.errors.empty());
    const auto bad_v = validate(bad, prefix);
    CHECK(!bad_v.ok);
    CHECK(!bad_v.upstream_configured);

    auto bad_url = cfg;
    bad_url.gateway.upstream_http = "127.0.0.1:7890";
    bad_url.gateway.upstream_socks5.clear();
    const auto bad_url_v = validate(bad_url, prefix);
    CHECK(!bad_url_v.ok);

    auto bad_port = cfg;
    bad_port.gateway.http_port = 0;
    CHECK(!validate(bad_port, prefix).ok);
}

static void test_jsonl_read_write_and_sample() {
    const auto prefix = unique_prefix("jsonl");
    const auto path = default_log_path(prefix);

    LogEntry proxy;
    proxy.src = "192.168.87.10:49152";
    proxy.dst_ip = "104.16.0.1";
    proxy.dst_port = 443;
    proxy.sni_or_host = "api.anthropic.com";
    proxy.proto = "tcp";
    proxy.action = "proxy";
    proxy.upstream_ok = true;
    proxy.bytes = 4096;
    const auto line = append_log_entry(path, proxy);
    CHECK(contains(line, "\"action\":\"proxy\""));
    CHECK(contains(line, "\"sni_or_host\":\"api.anthropic.com\""));
    CHECK(contains(line, "\"upstream_ok\":true"));
    CHECK(contains(line, "\"bytes\":4096"));

    LogEntry deny;
    deny.src = "192.168.87.10:49153";
    deny.dst_ip = "142.250.0.1";
    deny.dst_port = 443;
    deny.sni_or_host = "www.google-analytics.com";
    deny.action = "deny";
    deny.upstream_ok = false;
    deny.note = "blocklist";
    append_log_entry(path, deny);

    const auto sum = read_log(path, 8);
    CHECK(sum.file_exists);
    CHECK_EQ(sum.total, size_t{2});
    CHECK_EQ(sum.proxy_count, size_t{1});
    CHECK_EQ(sum.deny_count, size_t{1});
    CHECK_EQ(sum.recent.size(), size_t{2});
    CHECK_EQ(sum.recent[0].sni_or_host, std::string("api.anthropic.com"));
    CHECK_EQ(sum.recent[1].action, std::string("deny"));
    CHECK(sum.recent[1].note == "blocklist");
    CHECK(sum.recent[1].upstream_ok.has_value() && !*sum.recent[1].upstream_ok);

    const auto sample = read_log(sample_log(), 10);
    CHECK(sample.file_exists);
    CHECK(sample.total >= 3);
    CHECK(sample.deny_count >= 1);
    CHECK(sample.proxy_count >= 1);
    bool saw_anthropic = false;
    bool saw_analytics_deny = false;
    for (const auto& e : sample.recent) {
        if (e.sni_or_host == "api.anthropic.com" && e.action == "proxy") {
            saw_anthropic = true;
        }
        if (e.sni_or_host == "www.google-analytics.com" && e.action == "deny") {
            saw_analytics_deny = true;
        }
    }
    CHECK(saw_anthropic);
    CHECK(saw_analytics_deny);

    LogEntry parsed;
    CHECK(parse_log_line(
        "{\"ts\":\"t\",\"src\":\"s\",\"dst_ip\":\"1.2.3.4\",\"dst_port\":443,"
        "\"sni_or_host\":\"h\",\"proto\":\"tcp\",\"action\":\"allow\","
        "\"upstream_ok\":null,\"bytes\":null}",
        &parsed));
    CHECK_EQ(parsed.action, std::string("allow"));
    CHECK(!parsed.upstream_ok.has_value());
    CHECK(!parsed.bytes.has_value());
}

static void test_status_summary() {
    const auto prefix = unique_prefix("status");
    auto cfg = BoxConfig::load_file(example_config());
    const auto missing = status(cfg, prefix);
    CHECK(missing.upstream_configured);
    CHECK_EQ(missing.planned_listen, std::string("http://0.0.0.0:3080"));
    CHECK(!missing.log_file_exists);
    const auto text = format_status(missing);
    CHECK(contains(text, "已配置"));
    CHECK(contains(text, "http://127.0.0.1:7890"));
    CHECK(contains(text, "gost -C "));
    CHECK(contains(text, "gost.relay.yml"));
    CHECK(contains(text, "未启动 gost"));
    CHECK(contains(text, "尚不存在"));
    CHECK(!contains(text, "Win11 网关进程已启动"));

    fs::create_directories(default_log_path(prefix).parent_path());
    fs::copy_file(sample_log(), default_log_path(prefix));
    const auto present = status(cfg, prefix, 8);
    CHECK(present.log_file_exists);
    CHECK(present.log.deny_count >= 1);
    const auto present_text = format_status(present);
    CHECK(contains(present_text, "存在"));
    CHECK(contains(present_text, "api.anthropic.com"));
    CHECK(contains(present_text, "www.google-analytics.com"));
    CHECK(contains(present_text, "deny"));
}

static void test_config_round_trip_upstream_and_log_path() {
    const auto prefix = unique_prefix("roundtrip");
    fs::create_directories(prefix / "config");
    fs::create_directories(prefix / "var");
    const auto cfg_path = prefix / "config" / "wsl-box.yaml";
    fs::copy_file(example_config(), cfg_path);

    Options opt;
    opt.prefix = prefix;
    opt.config = cfg_path;
    auto session = Session::load(opt);
    session.config.gateway.upstream_http = "http://127.0.0.1:18080";
    session.config.gateway.upstream_socks5 = "socks5://127.0.0.1:18081";
    session.config.gateway.logging.enabled = true;
    session.config.gateway.logging.path = (prefix / "var" / "custom-gateway.jsonl").string();
    const auto saved = session.save();
    CHECK(saved.ok);
    if (!saved.ok) {
        std::cerr << "save error: " << saved.error << "\n";
    }

    const auto reloaded_cfg = BoxConfig::load_file(cfg_path);
    CHECK_EQ(reloaded_cfg.gateway.upstream_http, std::string("http://127.0.0.1:18080"));
    CHECK_EQ(reloaded_cfg.gateway.upstream_socks5, std::string("socks5://127.0.0.1:18081"));
    CHECK_EQ(reloaded_cfg.gateway.logging.path,
             (prefix / "var" / "custom-gateway.jsonl").string());
    CHECK(reloaded_cfg.gateway.logging.enabled);

    session.reload();
    CHECK_EQ(session.config.gateway.upstream_http, std::string("http://127.0.0.1:18080"));
    CHECK_EQ(session.config.gateway.logging.path,
             (prefix / "var" / "custom-gateway.jsonl").string());

    const auto planned = plan_relay(session.config, prefix);
    CHECK_EQ(planned.upstream_url, std::string("http://127.0.0.1:18080"));
    CHECK_EQ(planned.logging_path, prefix / "var" / "custom-gateway.jsonl");

    // Empty log path must stay empty in the saved yaml, but resolve under prefix.
    session.config.gateway.logging.path.clear();
    CHECK(session.save().ok);
    const auto empty_saved = BoxConfig::load_file(cfg_path);
    CHECK_EQ(empty_saved.gateway.logging.path, std::string(""));
    CHECK_EQ(resolve_log_path(empty_saved.gateway.logging.path, prefix),
             default_log_path(prefix));

    // Saving with no upstreams must refuse and leave the previous config.
    const auto before = read_text(cfg_path);
    session.config.gateway.upstream_http.clear();
    session.config.gateway.upstream_socks5.clear();
    const auto refused = session.save();
    CHECK(!refused.ok);
    CHECK(contains(refused.error, "上游"));
    CHECK_EQ(read_text(cfg_path), before);
}

int main() {
    try {
        test_default_log_path_under_prefix();
        test_blocklist_matches_powershell_suffix();
        test_plan_and_validate();
        test_jsonl_read_write_and_sample();
        test_status_summary();
        test_config_round_trip_upstream_and_log_path();
    } catch (const std::exception& ex) {
        std::cerr << "EXCEPTION " << ex.what() << "\n";
        return 1;
    }
    std::cout << "passed=" << g_passed << " failed=" << g_failed << "\n";
    return g_failed == 0 ? 0 : 1;
}
