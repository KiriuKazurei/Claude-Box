#include "claudebox/companion.hpp"
#include "claudebox/config.hpp"
#include "claudebox/settings.hpp"
#include "claudebox/spoof.hpp"

#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>
#include <sys/wait.h>
#include <unistd.h>

using claudebox::companion::CompanionPlan;
using claudebox::companion::default_engine_path;
using claudebox::companion::default_profile_dir;
using claudebox::companion::format_status;
using claudebox::companion::gateway_proxy_url;
using claudebox::companion::install_intercept;
using claudebox::companion::is_forbidden_profile_path;
using claudebox::companion::is_host_default_browser;
using claudebox::companion::is_http_url;
using claudebox::companion::is_oauth_url;
using claudebox::companion::open_url;
using claudebox::companion::plan;
using claudebox::config::BoxConfig;
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
#ifndef CLAUDEBOX_OPEN_OAUTH_SCRIPT
#define CLAUDEBOX_OPEN_OAUTH_SCRIPT ""
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

static fs::path opener_script() {
    const char* env = std::getenv("CLAUDE_BOX_OPEN_OAUTH");
    if (env && *env) {
        return fs::path(env);
    }
    fs::path from_cmake(CLAUDEBOX_OPEN_OAUTH_SCRIPT);
    if (!from_cmake.empty()) {
        return from_cmake;
    }
    return fs::path("src/wsl-box/companion-browser/open-oauth-url.sh");
}

static std::string read_text(const fs::path& path) {
    std::ifstream in(path);
    return std::string((std::istreambuf_iterator<char>(in)),
                       std::istreambuf_iterator<char>());
}

static void write_text(const fs::path& path, const std::string& body, bool exec = false) {
    fs::create_directories(path.parent_path());
    std::ofstream out(path);
    out << body;
    out.close();
    if (exec) {
        fs::permissions(path,
                        fs::perms::owner_all | fs::perms::group_read | fs::perms::group_exec |
                            fs::perms::others_read | fs::perms::others_exec,
                        fs::perm_options::replace);
    }
}

static bool contains(const std::string& hay, const std::string& needle) {
    return hay.find(needle) != std::string::npos;
}

static fs::path unique_prefix(const std::string& tag) {
    const auto base = fs::temp_directory_path() / "claudebox-p8" /
                      (tag + "-" + std::to_string(::getpid()) + "-" +
                       std::to_string(static_cast<long>(std::time(nullptr))));
    fs::remove_all(base);
    fs::create_directories(base);
    return base;
}

static BoxConfig load_example() {
    return BoxConfig::load_file(example_config());
}

static fs::path write_mock_engine(const fs::path& path, const fs::path& record_dir) {
    fs::create_directories(record_dir);
    std::ostringstream body;
    body << "#!/usr/bin/env bash\n"
         << "set -euo pipefail\n"
         << "REC=" << record_dir.string() << "\n"
         << "mkdir -p \"$REC\"\n"
         << "printf '%s\\n' \"$@\" > \"$REC/argv.txt\"\n"
         << "env | sort > \"$REC/env.txt\"\n"
         << "echo \"mock-engine opened: $*\"\n"
         << "exit 0\n";
    write_text(path, body.str(), true);
    return path;
}

static fs::path write_host_trap(const fs::path& dir) {
    fs::create_directories(dir);
    const char* names[] = {"xdg-open", "chrome", "google-chrome", "msedge", "www-browser",
                           "x-www-browser"};
    const std::string body =
        "#!/usr/bin/env bash\n"
        "echo \"HOST BROWSER TRAP invoked: $0 $*\" >> \"$(dirname \"$0\")/trap.log\"\n"
        "echo \"HOST BROWSER TRAP invoked: $0 $*\" >&2\n"
        "exit 0\n";
    for (const char* name : names) {
        write_text(dir / name, body, true);
    }
    return dir;
}

static int run_cmd(const std::string& cmd, std::string* output) {
    std::string full = cmd + " 2>&1";
    FILE* pipe = popen(full.c_str(), "r");
    if (!pipe) {
        if (output) {
            *output = "popen failed";
        }
        return 127;
    }
    std::string captured;
    char buf[4096];
    while (fgets(buf, sizeof(buf), pipe) != nullptr) {
        captured.append(buf);
    }
    const int st = pclose(pipe);
    if (output) {
        *output = captured;
    }
    if (WIFEXITED(st)) {
        return WEXITSTATUS(st);
    }
    return 1;
}

static void clear_engine_env() {
    unsetenv("CLAUDE_BOX_COMPANION_BROWSER");
    unsetenv("CLAUDE_BOX_COMPANION_PROFILE");
}

static void test_url_helpers() {
    CHECK(is_http_url("https://claude.ai/login"));
    CHECK(is_http_url("http://example.test/authorize"));
    CHECK(!is_http_url("file:///etc/passwd"));
    CHECK(!is_http_url("chrome://settings"));
    CHECK(is_oauth_url("https://claude.ai/login"));
    CHECK(is_oauth_url("https://console.anthropic.com/oauth/authorize?x=1"));
    CHECK(is_oauth_url("https://accounts.claude.ai/callback"));
}

static void test_forbidden_paths() {
    const auto prefix = unique_prefix("paths");
    CHECK(is_forbidden_profile_path(
        "/mnt/c/Users/zero/AppData/Local/Google/Chrome/User Data", prefix));
    CHECK(is_forbidden_profile_path(
        "/home/user/.config/google-chrome", prefix));
    CHECK(is_forbidden_profile_path(
        "C:\\Users\\zero\\AppData\\Roaming\\Claude", prefix));
    CHECK(!is_forbidden_profile_path(default_profile_dir(prefix), prefix));
    CHECK(is_host_default_browser("/usr/bin/xdg-open", prefix));
    CHECK(is_host_default_browser("/usr/bin/google-chrome", prefix));
    CHECK(is_host_default_browser("/usr/bin/msedge", prefix));
    CHECK(!is_host_default_browser(default_engine_path(prefix), prefix));
    fs::remove_all(prefix);
}

static void test_missing_engine_plan() {
    clear_engine_env();
    const auto prefix = unique_prefix("missing");
    const auto cfg = load_example();
    CHECK(cfg.features.oauth_open_in_fingerprint_browser);
    const auto p = plan(cfg, prefix, "https://claude.ai/login");
    CHECK(!p.ok);
    CHECK(!p.engine_ready);
    CHECK(!p.would_use_host_browser);
    CHECK(p.feature_enabled);
    CHECK(p.proxy_configured);
    CHECK_EQ(p.timezone, std::string("America/Los_Angeles"));
    CHECK(contains(p.http_proxy, "http://127.0.0.1:3080") ||
          contains(gateway_proxy_url(cfg), "3080"));
    bool refused = false;
    for (const auto& e : p.errors) {
        if (contains(e, "Refusing to open the host default browser")) {
            refused = true;
        }
    }
    CHECK(refused);
    const auto status = format_status(p);
    CHECK(contains(status, "未就绪"));
    CHECK(contains(status, "不会回退") || contains(status, "绝不会改用宿主"));
    CHECK(contains(status, "America/Los_Angeles"));
    CHECK(contains(status, "http://127.0.0.1:3080"));

    const auto opened = open_url(cfg, prefix, "https://claude.ai/login", false);
    CHECK(!opened.ok);
    CHECK(!opened.launched_host_browser);
    CHECK(opened.exit_code != 0);
    CHECK(contains(opened.error, "Refusing to open the host default browser"));
    fs::remove_all(prefix);
}

static void test_open_with_mock_engine() {
    clear_engine_env();
    const auto prefix = unique_prefix("open-mock");
    const auto record = prefix / "record";
    const auto engine = write_mock_engine(default_engine_path(prefix), record);
    const auto trap = write_host_trap(prefix / "host-trap");
    (void)engine;

    auto cfg = load_example();
    cfg.identity.timezone = "America/Los_Angeles";
    cfg.identity.locale = "en_US.UTF-8";
    cfg.identity.language = "en_US";
    cfg.identity.electron_lang = "en-US";

    const auto planned = plan(cfg, prefix, "https://claude.ai/login?oauth=1");
    CHECK(planned.ok);
    CHECK(planned.engine_ready);
    CHECK(!planned.would_use_host_browser);
    CHECK_EQ(planned.timezone, std::string("America/Los_Angeles"));
    CHECK(contains(planned.http_proxy, "http://127.0.0.1:3080"));
    CHECK(contains(planned.command_line, "--user-data-dir="));
    CHECK(contains(planned.profile_dir.string(), (prefix / "var" / "companion-browser" / "profile").string()));
    CHECK(!is_forbidden_profile_path(planned.profile_dir, prefix));
    bool has_tz = false;
    bool has_proxy = false;
    for (const auto& [k, v] : planned.env) {
        if (k == "TZ" && v == "America/Los_Angeles") {
            has_tz = true;
        }
        if (k == "HTTP_PROXY" && contains(v, "127.0.0.1:3080")) {
            has_proxy = true;
        }
    }
    CHECK(has_tz);
    CHECK(has_proxy);

    const auto dry = open_url(cfg, prefix, "https://claude.ai/login?oauth=1", true);
    CHECK(dry.ok);
    CHECK_EQ(dry.exit_code, 0);
    CHECK(!fs::exists(record / "argv.txt"));

    const auto opened = open_url(cfg, prefix, "https://claude.ai/login?oauth=1", false);
    CHECK(opened.ok);
    CHECK(!opened.launched_host_browser);
    CHECK_EQ(opened.exit_code, 0);
    CHECK(contains(opened.output, "mock-engine opened"));
    CHECK(fs::exists(record / "argv.txt"));
    const auto argv = read_text(record / "argv.txt");
    CHECK(contains(argv, "https://claude.ai/login?oauth=1"));
    CHECK(contains(argv, "--user-data-dir=" + planned.profile_dir.string()));
    CHECK(contains(argv, "--proxy-server=http://127.0.0.1:3080"));
    CHECK(contains(argv, "--lang=en-US"));
    const auto envtxt = read_text(record / "env.txt");
    CHECK(contains(envtxt, "TZ=America/Los_Angeles"));
    CHECK(contains(envtxt, "LANG=en_US.UTF-8"));
    CHECK(contains(envtxt, "HTTP_PROXY=http://127.0.0.1:3080"));
    CHECK(contains(envtxt, "HTTPS_PROXY=http://127.0.0.1:3080"));
    CHECK(!fs::exists(trap / "trap.log"));

    const auto status = format_status(planned);
    CHECK(contains(status, "已就绪"));
    CHECK(contains(status, planned.profile_dir.string()));
    CHECK(contains(status, "http://127.0.0.1:3080"));
    fs::remove_all(prefix);
}

static void test_host_browser_as_engine_refused() {
    clear_engine_env();
    const auto prefix = unique_prefix("host-engine");
    setenv("CLAUDE_BOX_COMPANION_BROWSER", "/usr/bin/xdg-open", 1);
    const auto cfg = load_example();
    const auto p = plan(cfg, prefix, "https://claude.ai/login");
    CHECK(!p.ok);
    CHECK(!p.engine_ready);
    CHECK(!p.would_use_host_browser);
    bool refused = false;
    for (const auto& e : p.errors) {
        if (contains(e, "host default browser") || contains(e, "xdg-open")) {
            refused = true;
        }
    }
    CHECK(refused);
    unsetenv("CLAUDE_BOX_COMPANION_BROWSER");
    fs::remove_all(prefix);
}

static void test_forbidden_profile_rejected() {
    clear_engine_env();
    const auto prefix = unique_prefix("bad-profile");
    write_mock_engine(default_engine_path(prefix), prefix / "record");
    setenv("CLAUDE_BOX_COMPANION_PROFILE",
           "/mnt/c/Users/zero/AppData/Local/Google/Chrome/User Data", 1);
    const auto cfg = load_example();
    const auto p = plan(cfg, prefix, "https://claude.ai/login");
    CHECK(!p.ok);
    bool refused = false;
    for (const auto& e : p.errors) {
        if (contains(e, "Chrome") || contains(e, "userData") || contains(e, "profile")) {
            refused = true;
        }
    }
    CHECK(refused);
    unsetenv("CLAUDE_BOX_COMPANION_PROFILE");
    fs::remove_all(prefix);
}

static void test_feature_flag_off_no_host_fallback() {
    clear_engine_env();
    const auto prefix = unique_prefix("flag-off");
    write_mock_engine(default_engine_path(prefix), prefix / "record");
    auto cfg = load_example();
    cfg.features.oauth_open_in_fingerprint_browser = false;
    const auto opened = open_url(cfg, prefix, "https://claude.ai/login", false);
    CHECK(!opened.ok);
    CHECK(!opened.launched_host_browser);
    CHECK(opened.exit_code != 0);
    CHECK(contains(opened.error, "disabled") ||
          contains(opened.error, "host default browser"));
    CHECK(!fs::exists(prefix / "record" / "argv.txt"));
    fs::remove_all(prefix);
}

static void test_missing_proxy_no_bypass() {
    clear_engine_env();
    const auto prefix = unique_prefix("no-proxy");
    write_mock_engine(default_engine_path(prefix), prefix / "record");
    auto cfg = load_example();
    cfg.gateway.upstream_http.clear();
    cfg.gateway.upstream_socks5.clear();
    const auto p = plan(cfg, prefix, "https://claude.ai/login");
    CHECK(!p.ok);
    CHECK(!p.proxy_configured);
    bool refused = false;
    for (const auto& e : p.errors) {
        if (contains(e, "bypass") || contains(e, "gateway")) {
            refused = true;
        }
    }
    CHECK(refused);
    fs::remove_all(prefix);
}

static int run_with_path(const fs::path& prefix, const fs::path& trap, const std::string& cmd,
                         std::string* output) {
    std::ostringstream full;
    full << "env PATH=" << trap.string() << ":" << (prefix / "bin").string()
         << ":$PATH CLAUDE_BOX_PREFIX=" << prefix.string()
         << " CLAUDE_BOX_OPEN_OAUTH=" << opener_script().string() << " " << cmd;
    return run_cmd(full.str(), output);
}

static void test_intercept_and_shell_opener() {
    clear_engine_env();
    const auto prefix = unique_prefix("intercept");
    const auto record = prefix / "record";
    write_mock_engine(default_engine_path(prefix), record);
    const auto trap = write_host_trap(prefix / "host-trap");

    auto cfg = load_example();
    const auto spoofed = claudebox::spoof::apply_config(cfg.identity, prefix);
    CHECK(spoofed.ok);

    const auto installed = install_intercept(cfg, prefix);
    CHECK(installed.ok);
    CHECK(fs::exists(prefix / "bin" / "xdg-open"));
    CHECK(fs::exists(prefix / "bin" / "www-browser"));
    CHECK(fs::exists(prefix / "bin" / "open-oauth-url"));
    CHECK(fs::exists(prefix / "env" / "companion.env"));
    const auto companion_env = read_text(prefix / "env" / "companion.env");
    CHECK(contains(companion_env, (prefix / "bin" / "xdg-open").string()));
    CHECK(contains(companion_env, "BROWSER="));
    const auto session = read_text(prefix / "env" / "session.env");
    CHECK(contains(session, "companion.env"));
    CHECK(!contains(session, "BROWSER=xdg-open"));

    // Missing engine via the repo script (smoke contract).
    std::string missing_out;
    const int missing_rc = run_cmd(
        "env -u CLAUDE_BOX_COMPANION_BROWSER CLAUDE_BOX_PREFIX=/nonexistent/prefix-no-engine "
        "CLAUDE_BOX_OAUTH_OPEN_IN_FINGERPRINT_BROWSER=true "
        "bash " +
            opener_script().string() + " https://claude.ai/login",
        &missing_out);
    CHECK(missing_rc != 0);
    CHECK(contains(missing_out, "Refusing to open the host default browser"));

    // Intercept + mock engine: must go through companion, not the trap.
    std::string opened_out;
    const int opened_rc = run_with_path(
        prefix, trap,
        "HTTP_PROXY=http://127.0.0.1:3080 HTTPS_PROXY=http://127.0.0.1:3080 "
        "ALL_PROXY=http://127.0.0.1:3080 "
        "bash " +
            (prefix / "bin" / "xdg-open").string() + " https://claude.ai/login",
        &opened_out);
    CHECK_EQ(opened_rc, 0);
    CHECK(contains(opened_out, "mock-engine opened") ||
          contains(opened_out, "companion-browser"));
    CHECK(fs::exists(record / "argv.txt"));
    CHECK(contains(read_text(record / "argv.txt"), "https://claude.ai/login"));
    CHECK(contains(read_text(record / "env.txt"), "TZ=America/Los_Angeles") ||
          contains(read_text(record / "env.txt"), "HTTP_PROXY=http://127.0.0.1:3080"));
    CHECK(!fs::exists(trap / "trap.log"));

    // www-browser wrapper, same path.
    fs::remove(record / "argv.txt");
    std::string www_out;
    const int www_rc = run_with_path(
        prefix, trap,
        "HTTP_PROXY=http://127.0.0.1:3080 "
        "bash " +
            (prefix / "bin" / "www-browser").string() +
            " https://console.anthropic.com/oauth/authorize",
        &www_out);
    CHECK_EQ(www_rc, 0);
    CHECK(contains(read_text(record / "argv.txt"), "https://console.anthropic.com/oauth/authorize"));
    CHECK(!fs::exists(trap / "trap.log"));
    fs::remove_all(prefix);
}

static void test_settings_save_installs_intercept() {
    clear_engine_env();
    const auto prefix = unique_prefix("settings-save");
    fs::create_directories(prefix / "config");
    fs::create_directories(prefix / "var");
    fs::copy_file(example_config(), prefix / "config" / "wsl-box.yaml");

    claudebox::settings::Options opt;
    opt.prefix = prefix;
    opt.config = prefix / "config" / "wsl-box.yaml";
    auto session = Session::load(opt);
    const auto saved = session.save();
    CHECK(saved.ok);
    CHECK(fs::exists(prefix / "bin" / "xdg-open"));
    CHECK(fs::exists(prefix / "env" / "companion.env"));
    const auto session_env = read_text(prefix / "env" / "session.env");
    CHECK(contains(session_env, "companion.env"));
    CHECK(contains(session_env, "TZ=") || contains(read_text(prefix / "env" / "spoof.env"), "TZ="));
    fs::remove_all(prefix);
}

int main() {
    try {
        test_url_helpers();
        test_forbidden_paths();
        test_missing_engine_plan();
        test_open_with_mock_engine();
        test_host_browser_as_engine_refused();
        test_forbidden_profile_rejected();
        test_feature_flag_off_no_host_fallback();
        test_missing_proxy_no_bypass();
        test_intercept_and_shell_opener();
        test_settings_save_installs_intercept();
    } catch (const std::exception& ex) {
        std::cerr << "EXCEPTION " << ex.what() << "\n";
        return 1;
    }
    std::cout << "passed=" << g_passed << " failed=" << g_failed << "\n";
    return g_failed == 0 ? 0 : 1;
}
