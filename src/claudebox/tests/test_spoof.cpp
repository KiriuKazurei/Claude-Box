#include "claudebox/config.hpp"
#include "claudebox/settings.hpp"
#include "claudebox/spoof.hpp"

#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <unistd.h>
#include <iostream>
#include <sstream>
#include <string>
#include <system_error>

using claudebox::config::BoxConfig;
using claudebox::settings::Session;
using claudebox::spoof::SpoofValues;
using claudebox::spoof::apply;
using claudebox::spoof::apply_config;
using claudebox::spoof::format_status;
using claudebox::spoof::from_config;
using claudebox::spoof::locale_language_tag;
using claudebox::spoof::plan;
using claudebox::spoof::prefix_localtime_path;
using claudebox::spoof::prefix_timezone_path;
using claudebox::spoof::probe_intl;
using claudebox::spoof::session_env_path;
using claudebox::spoof::spoof_env_path;
using claudebox::spoof::timezone_allowed;
using claudebox::spoof::verify;

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
#ifndef CLAUDEBOX_FIXTURE_DIR
#define CLAUDEBOX_FIXTURE_DIR ""
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

static fs::path fixture_inventory() {
    const char* env = std::getenv("CLAUDEBOX_FIXTURE");
    if (env && *env) {
        return fs::path(env);
    }
    fs::path from_cmake(CLAUDEBOX_FIXTURE_DIR);
    if (!from_cmake.empty()) {
        return from_cmake / "host-inventory.raw.yaml";
    }
    return fs::path("tests/wsl-box/fixtures/host-inventory.raw.yaml");
}

static fs::path make_tmpdir(const std::string& name) {
    const auto dir = fs::temp_directory_path() / ("claudebox-p7-" + name + "-" +
                                                   std::to_string(std::rand()));
    fs::remove_all(dir);
    fs::create_directories(dir);
    return dir;
}

static std::string read_text(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream out;
    out << in.rdbuf();
    return out.str();
}

static bool contains(const std::string& hay, const std::string& needle) {
    return hay.find(needle) != std::string::npos;
}

struct HostEtcSnapshot {
    bool exists = false;
    bool is_symlink = false;
    std::string target;
    std::uintmax_t size = 0;
    fs::file_time_type mtime{};
};

static HostEtcSnapshot snapshot_host_localtime() {
    HostEtcSnapshot snap;
    const fs::path path("/etc/localtime");
    std::error_code ec;
    snap.exists = fs::exists(path, ec);
    snap.is_symlink = fs::is_symlink(path, ec);
    if (snap.is_symlink) {
        snap.target = fs::read_symlink(path, ec).string();
    }
    if (snap.exists) {
        snap.size = fs::file_size(path, ec);
        snap.mtime = fs::last_write_time(path, ec);
    }
    return snap;
}

static void test_locale_tag() {
    CHECK_EQ(locale_language_tag("en_US.UTF-8"), std::string("en-US"));
    CHECK_EQ(locale_language_tag("de_DE.UTF-8"), std::string("de-DE"));
    CHECK_EQ(locale_language_tag("en-US"), std::string("en-US"));
}

static void test_rejects_bad_timezone() {
    std::string err;
    CHECK(!timezone_allowed("", &err));
    CHECK(!timezone_allowed("../etc/passwd", &err));
    CHECK(!timezone_allowed("/etc/localtime", &err));
    CHECK(!timezone_allowed("Not/AZone", &err));
    CHECK(timezone_allowed("America/Los_Angeles", &err));
    CHECK(timezone_allowed("Europe/Berlin", &err));
}

static void test_example_config_apply_and_intl() {
    const auto cfg = BoxConfig::load_file(example_config());
    CHECK_EQ(cfg.identity.timezone, std::string("America/Los_Angeles"));
    CHECK_EQ(cfg.identity.locale, std::string("en_US.UTF-8"));
    CHECK_EQ(cfg.identity.language, std::string("en_US"));

    const auto before = snapshot_host_localtime();
    const auto prefix = make_tmpdir("example");
    const auto values = from_config(cfg.identity);
    const auto planned = plan(values, prefix);
    CHECK(planned.ok);
    CHECK(!planned.writes_host_etc);

    const auto applied = apply_config(cfg.identity, prefix);
    CHECK(applied.ok);
    if (!applied.ok) {
        std::cerr << "apply error: " << applied.error << "\n";
    }

    CHECK(fs::exists(spoof_env_path(prefix)));
    CHECK(fs::exists(session_env_path(prefix)));
    CHECK(fs::exists(prefix_timezone_path(prefix)));
    CHECK(fs::exists(prefix_localtime_path(prefix)) ||
          fs::is_symlink(prefix_localtime_path(prefix)));

    const auto spoof_env = read_text(spoof_env_path(prefix));
    CHECK(contains(spoof_env, "America/Los_Angeles"));
    CHECK(contains(spoof_env, "en_US.UTF-8"));
    CHECK(!contains(spoof_env, "Asia/Tokyo"));
    CHECK(contains(spoof_env, "SENTRY_NAME="));
    CHECK(contains(spoof_env, "claude-box"));
    CHECK(contains(spoof_env, "ICU_TIMEZONE_FILES_DIR="));
    CHECK(contains(spoof_env, (prefix / "etc" / "zoneinfo").string()));

    const auto session = read_text(session_env_path(prefix));
    CHECK(contains(session, "America/Los_Angeles"));
    CHECK(contains(session, "export TZ="));
    CHECK(contains(session, "SENTRY_NAME="));
    CHECK(contains(session, "ICU_TIMEZONE_FILES_DIR="));
    CHECK(contains(session, "claude-box"));

    char hostbuf[256] = {};
    if (gethostname(hostbuf, sizeof(hostbuf) - 1) == 0 && hostbuf[0] != '\0') {
        const std::string real_host(hostbuf);
        if (real_host != "claude-box") {
            CHECK(!contains(spoof_env, "SENTRY_NAME=\"" + real_host));
            CHECK(!contains(session, "SENTRY_NAME=\"" + real_host));
        }
    }

    const auto icu_dir = prefix / "etc" / "zoneinfo";
    CHECK(fs::exists(icu_dir / "America" / "Los_Angeles") ||
          fs::is_symlink(icu_dir / "America" / "Los_Angeles"));
    CHECK(!fs::exists(icu_dir / "Asia" / "Tokyo"));

    const auto verified = verify(values, prefix);
    CHECK(verified.ok);
    if (!verified.ok) {
        for (const auto& issue : verified.issues) {
            std::cerr << "verify issue: " << issue << "\n";
        }
    }
    if (verified.probe.ran) {
        CHECK(verified.probe.ok);
        CHECK_EQ(verified.probe.timezone, std::string("America/Los_Angeles"));
        CHECK(contains(verified.probe.locale, "en"));
        CHECK(verified.probe.timezone != std::string("Asia/Tokyo"));
    } else {
        std::cerr << "NOTE: Intl probe did not run\n";
        for (const auto& w : verified.probe.warnings) {
            std::cerr << "  warning: " << w << "\n";
        }
    }

    const auto status = format_status(planned, &verified);
    CHECK(contains(status, "盒子内的伪装值"));
    CHECK(contains(status, "America/Los_Angeles"));
    CHECK(contains(status, "不会写入宿主") || contains(status, "不会改宿主"));
    CHECK(contains(status, "没有") && contains(status, "宿主真实时区"));

    const auto after = snapshot_host_localtime();
    CHECK_EQ(before.exists, after.exists);
    CHECK_EQ(before.is_symlink, after.is_symlink);
    CHECK_EQ(before.target, after.target);
    CHECK_EQ(before.size, after.size);
    CHECK(before.mtime == after.mtime);

    fs::remove_all(prefix);
}

static void test_change_reapply() {
    const auto before = snapshot_host_localtime();
    const auto prefix = make_tmpdir("reapply");
    SpoofValues first;
    first.timezone = "America/Los_Angeles";
    first.locale = "en_US.UTF-8";
    first.language = "en_US";
    first.electron_lang = "en-US";
    auto applied = apply(first, prefix);
    CHECK(applied.ok);
    auto verified = verify(first, prefix);
    CHECK(verified.ok);
    if (verified.probe.ran) {
        CHECK_EQ(verified.probe.timezone, std::string("America/Los_Angeles"));
    }

    // Preserve an unrelated session export across re-apply.
    {
        std::ofstream out(session_env_path(prefix), std::ios::app);
        out << "export HTTP_PROXY=\"http://127.0.0.1:3080\"\n";
    }

    SpoofValues second = first;
    second.timezone = "Europe/Berlin";
    second.locale = "de_DE.UTF-8";
    second.language = "de_DE";
    second.electron_lang = "de-DE";
    second.hostname = "lab-box";
    applied = apply(second, prefix);
    CHECK(applied.ok);
    verified = verify(second, prefix);
    CHECK(verified.ok);
    if (!verified.ok) {
        for (const auto& issue : verified.issues) {
            std::cerr << "reapply issue: " << issue << "\n";
        }
    }
    if (verified.probe.ran) {
        CHECK(verified.probe.ok);
        CHECK_EQ(verified.probe.timezone, std::string("Europe/Berlin"));
        CHECK_EQ(verified.probe.locale, std::string("de-DE"));
        CHECK(verified.probe.timezone != std::string("Asia/Tokyo"));
        CHECK(verified.probe.timezone != std::string("America/Los_Angeles"));
    }

    const auto spoof_env = read_text(spoof_env_path(prefix));
    CHECK(contains(spoof_env, "Europe/Berlin"));
    CHECK(!contains(spoof_env, "America/Los_Angeles"));
    CHECK(contains(spoof_env, "de_DE.UTF-8"));
    CHECK(contains(spoof_env, "SENTRY_NAME="));
    CHECK(contains(spoof_env, "lab-box"));
    CHECK(contains(read_text(session_env_path(prefix)), "SENTRY_NAME="));
    CHECK(contains(read_text(session_env_path(prefix)), "lab-box"));
    CHECK(fs::exists(prefix / "etc" / "zoneinfo" / "Europe" / "Berlin") ||
          fs::is_symlink(prefix / "etc" / "zoneinfo" / "Europe" / "Berlin"));
    CHECK(!fs::exists(prefix / "etc" / "zoneinfo" / "Asia" / "Tokyo"));

    const auto session = read_text(session_env_path(prefix));
    CHECK(contains(session, "Europe/Berlin"));
    CHECK(contains(session, "de_DE.UTF-8"));
    CHECK(contains(session, "HTTP_PROXY="));
    CHECK(!contains(session, "America/Los_Angeles"));
    CHECK(contains(read_text(prefix_timezone_path(prefix)), "Europe/Berlin"));

    const auto after = snapshot_host_localtime();
    CHECK(before.mtime == after.mtime);
    CHECK_EQ(before.target, after.target);

    fs::remove_all(prefix);
}

static void test_rejects_bad_plan() {
    const auto prefix = make_tmpdir("bad");
    SpoofValues bad;
    bad.timezone = "Not/AZone";
    bad.locale = "en_US.UTF-8";
    bad.language = "en_US";
    const auto planned = plan(bad, prefix);
    CHECK(!planned.ok);
    const auto applied = apply(bad, prefix);
    CHECK(!applied.ok);
    CHECK(!fs::exists(spoof_env_path(prefix)));

    SpoofValues traverse;
    traverse.timezone = "../etc/passwd";
    traverse.locale = "en_US.UTF-8";
    traverse.language = "en_US";
    CHECK(!apply(traverse, prefix).ok);

    fs::remove_all(prefix);
}

static Session make_session(const fs::path& prefix) {
    fs::create_directories(prefix / "config");
    fs::create_directories(prefix / "var");
    fs::copy_file(example_config(), prefix / "config" / "wsl-box.yaml",
                  fs::copy_options::overwrite_existing);
    fs::copy_file(fixture_inventory(), prefix / "var" / "host-inventory.raw.yaml",
                  fs::copy_options::overwrite_existing);
    claudebox::settings::Options opt;
    opt.prefix = prefix;
    opt.config = prefix / "config" / "wsl-box.yaml";
    opt.inventory = prefix / "var" / "host-inventory.raw.yaml";
    return Session::load(opt);
}

static void test_session_save_applies_spoof() {
    const auto prefix = make_tmpdir("session");
    auto session = make_session(prefix);
    CHECK_EQ(session.config.identity.timezone, std::string("America/Los_Angeles"));

    auto saved = session.save();
    CHECK(saved.ok);
    if (!saved.ok) {
        std::cerr << "save error: " << saved.error << "\n";
    }
    auto verified = verify(from_config(session.config.identity), prefix);
    CHECK(verified.ok);
    if (verified.probe.ran) {
        CHECK_EQ(verified.probe.timezone, std::string("America/Los_Angeles"));
    }

    session.config.identity.timezone = "Europe/Berlin";
    session.config.identity.locale = "de_DE.UTF-8";
    session.config.identity.language = "de_DE";
    session.config.identity.electron_lang = "de-DE";
    saved = session.save();
    CHECK(saved.ok);
    session.reload();
    CHECK_EQ(session.config.identity.timezone, std::string("Europe/Berlin"));
    CHECK_EQ(session.config.identity.locale, std::string("de_DE.UTF-8"));

    verified = verify(from_config(session.config.identity), prefix);
    CHECK(verified.ok);
    if (verified.probe.ran) {
        CHECK_EQ(verified.probe.timezone, std::string("Europe/Berlin"));
        CHECK_EQ(verified.probe.locale, std::string("de-DE"));
    }
    CHECK(contains(read_text(spoof_env_path(prefix)), "Europe/Berlin"));
    CHECK(!contains(read_text(spoof_env_path(prefix)), "America/Los_Angeles"));

    // Host timezone is not a presentable inventory field.
    if (fs::exists(prefix / "var" / "host-identity.presented.yaml")) {
        const auto presented = read_text(prefix / "var" / "host-identity.presented.yaml");
        CHECK(!contains(presented, "Asia/Tokyo"));
        CHECK(!contains(presented, "timezone:"));
    }

    // Invalid timezone must fail before (or without leaving) a bad apply.
    session.config.identity.timezone = "Not/AZone";
    saved = session.save();
    CHECK(!saved.ok);

    fs::remove_all(prefix);
}

static void test_probe_uses_supplied_values_not_host() {
    SpoofValues values;
    values.timezone = "America/Los_Angeles";
    values.locale = "en_US.UTF-8";
    values.language = "en_US";
    const auto probe = probe_intl(values);
    if (probe.ran) {
        CHECK(probe.ok);
        CHECK_EQ(probe.timezone, std::string("America/Los_Angeles"));
        CHECK(probe.timezone != std::string("Asia/Tokyo"));
        for (const auto& w : probe.warnings) {
            std::cerr << "NOTE Intl warning: " << w << "\n";
        }
    } else {
        std::cerr << "NOTE: probe_intl did not run; skipping live Intl assertion\n";
    }
}

int main() {
    std::srand(static_cast<unsigned>(std::time(nullptr)));
    try {
        test_locale_tag();
        test_rejects_bad_timezone();
        test_example_config_apply_and_intl();
        test_change_reapply();
        test_rejects_bad_plan();
        test_session_save_applies_spoof();
        test_probe_uses_supplied_values_not_host();
    } catch (const std::exception& ex) {
        std::cerr << "EXCEPTION: " << ex.what() << "\n";
        return 1;
    }
    std::cout << "passed " << g_passed << "  failed " << g_failed << "\n";
    return g_failed == 0 ? 0 : 1;
}
