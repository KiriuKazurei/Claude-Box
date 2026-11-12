#include "claudebox/config.hpp"
#include "claudebox/identity.hpp"
#include "claudebox/settings.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

using claudebox::config::BoxConfig;
using claudebox::identity::FieldMode;
using claudebox::identity::Policy;
using claudebox::identity::field_mode_name;
using claudebox::identity::hidden_identifier_values;
using claudebox::identity::inventory_value;
using claudebox::identity::load_inventory_file;
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

static fs::path make_tmpdir(const std::string& tag) {
    auto dir = fs::temp_directory_path() / "claudebox-p4-tests" / tag;
    fs::remove_all(dir);
    fs::create_directories(dir);
    return dir;
}

static std::string read_text(const fs::path& path) {
    std::ifstream in(path);
    return std::string((std::istreambuf_iterator<char>(in)),
                       std::istreambuf_iterator<char>());
}

static bool contains(const std::string& hay, const std::string& needle) {
    return hay.find(needle) != std::string::npos;
}

static void test_load_example() {
    const auto cfg = BoxConfig::load_file(example_config());
    CHECK_EQ(cfg.api_version, std::string("claude-box/wsl/v1alpha1"));
    CHECK_EQ(cfg.kind, std::string("WslBox"));
    CHECK_EQ(cfg.name, std::string("default"));
    CHECK_EQ(cfg.distro_name, std::string("ClaudeBox"));
    CHECK_EQ(cfg.base.preferred_source, std::string("Ubuntu-24.04"));
    CHECK_EQ(cfg.base.install_dir, std::string(""));
    CHECK_EQ(cfg.gateway.host, std::string("auto"));
    CHECK_EQ(cfg.gateway.http_port, 3080);
    CHECK_EQ(cfg.gateway.upstream_http, std::string("http://127.0.0.1:7890"));
    CHECK_EQ(cfg.gateway.upstream_socks5, std::string("socks5://127.0.0.1:7891"));
    CHECK(cfg.gateway.blocklist == BoxConfig::defaults().gateway.blocklist);
    CHECK(std::find(cfg.gateway.blocklist.begin(), cfg.gateway.blocklist.end(),
                    "sentry.io") != cfg.gateway.blocklist.end());
    CHECK(std::find(cfg.gateway.blocklist.begin(), cfg.gateway.blocklist.end(),
                    "browser-intake-us3-datadoghq.com") != cfg.gateway.blocklist.end());
    CHECK(std::find(cfg.gateway.blocklist.begin(), cfg.gateway.blocklist.end(),
                    "browser-intake-ddog-gov.com") != cfg.gateway.blocklist.end());
    CHECK(std::find(cfg.gateway.blocklist.begin(), cfg.gateway.blocklist.end(),
                    "/api/event_logging") != cfg.gateway.blocklist.end());
    CHECK(std::find(cfg.gateway.blocklist.begin(), cfg.gateway.blocklist.end(),
                    "/api/v2/rum") != cfg.gateway.blocklist.end());
    CHECK(cfg.gateway.logging.enabled);
    CHECK_EQ(cfg.gateway.logging.path, std::string(""));
    CHECK_EQ(cfg.visible_dirs.size(), size_t{1});
    CHECK_EQ(cfg.visible_dirs[0].host_path, std::string("D:\\Projects\\demo"));
    CHECK_EQ(cfg.visible_dirs[0].guest_path, std::string("/mnt/claude-box/demo"));
    CHECK(!cfg.visible_dirs[0].read_only);
    CHECK(cfg.mount.disable_automount);
    CHECK_EQ(cfg.identity.timezone, std::string("America/Los_Angeles"));
    CHECK_EQ(cfg.identity.locale, std::string("en_US.UTF-8"));
    CHECK_EQ(cfg.identity.language, std::string("en_US"));
    CHECK_EQ(cfg.identity.hostname, std::string("claude-box"));
    CHECK_EQ(cfg.identity.electron_lang, std::string("en-US"));
    CHECK(cfg.host_identity.expose_real_model);
    CHECK(cfg.host_identity.hide_identifiers);
    CHECK_EQ(cfg.host_identity.hidden_identifier_keys.size(), size_t{12});
    CHECK_EQ(cfg.desktop.command, std::string("claude-desktop"));
    CHECK(cfg.desktop.extra_args.empty());
    CHECK(!cfg.desktop.asar_patch);
    CHECK(cfg.features.exclude_host_system_drive);
    CHECK(!cfg.features.computer_use);
    CHECK(!cfg.features.kill_on_local_ip_change);
    CHECK_EQ(cfg.features.kill_grace_seconds, 10);
    CHECK_EQ(cfg.features.watch_interval_seconds, 5);
    CHECK(cfg.features.oauth_open_in_fingerprint_browser);
    CHECK(cfg.features.identity_vault);
}

static void test_round_trip() {
    const auto original = BoxConfig::load_file(example_config());
    auto dir = make_tmpdir("roundtrip");
    const auto path = dir / "config" / "wsl-box.yaml";
    original.save_file(path);
    const auto loaded = BoxConfig::load_file(path);
    CHECK(original == loaded);

    // Mutate every supported section and round-trip again.
    BoxConfig edited = loaded;
    edited.name = "lab";
    edited.distro_name = "ClaudeBoxLab";
    edited.base.preferred_source = "Ubuntu-22.04";
    edited.base.install_dir = "C:\\Local\\ClaudeBox";  // install_dir may mention C:; not a mount
    edited.gateway.host = "172.20.0.1";
    edited.gateway.http_port = 4080;
    edited.gateway.upstream_http = "http://127.0.0.1:7897";
    edited.gateway.upstream_socks5 = "socks5://127.0.0.1:7898";
    edited.gateway.blocklist = {"example.blocked.test"};
    edited.gateway.logging.enabled = false;
    edited.gateway.logging.path = "/tmp/gw.jsonl";
    edited.visible_dirs.push_back({"E:\\Notes", "/mnt/claude-box/notes", true});
    edited.mount.disable_automount = false;
    edited.identity.timezone = "Europe/Berlin";
    edited.identity.locale = "de_DE.UTF-8";
    edited.identity.language = "de_DE";
    edited.identity.hostname = "lab-box";
    edited.identity.electron_lang = "de-DE";
    edited.host_identity.expose_real_model = false;
    edited.host_identity.manufacturer = "Contoso Computers";
    edited.host_identity.product = "AdventureBook 14";
    edited.desktop.deb_url = "https://example.test/claude.deb";
    edited.desktop.deb_local = "/tmp/claude.deb";
    edited.desktop.command = "claude-desktop";
    edited.desktop.extra_args = {"--ozone-platform=x11"};
    edited.desktop.asar_patch = true;
    edited.features.computer_use = true;
    edited.features.kill_on_local_ip_change = true;
    edited.features.kill_grace_seconds = 15;
    edited.features.watch_interval_seconds = 3;
    edited.features.oauth_open_in_fingerprint_browser = false;
    edited.features.identity_vault = false;
    edited.features.exclude_host_system_drive = true;
    const auto path2 = dir / "config" / "edited.yaml";
    edited.save_file(path2);
    const auto reloaded = BoxConfig::load_file(path2);
    CHECK(edited == reloaded);
    const auto text = read_text(path2);
    CHECK(contains(text, "Europe/Berlin"));
    CHECK(contains(text, "4090") == false);
    CHECK(contains(text, "4080"));
    CHECK(contains(text, "--ozone-platform=x11"));
    CHECK(contains(text, "E:\\\\Notes") || contains(text, "E:\\Notes"));
}

static void test_refuse_example_and_force_hide() {
    bool threw = false;
    try {
        BoxConfig::defaults().save_file(example_config());
    } catch (const std::exception& ex) {
        threw = std::string(ex.what()).find("example") != std::string::npos;
    }
    CHECK(threw);

    auto dir = make_tmpdir("force-hide");
    BoxConfig cfg = BoxConfig::defaults();
    cfg.host_identity.hide_identifiers = false;
    const auto path = dir / "wsl-box.yaml";
    cfg.save_file(path);
    const auto loaded = BoxConfig::load_file(path);
    CHECK(loaded.host_identity.hide_identifiers);
    CHECK(contains(read_text(path), "hide_identifiers: true"));
    CHECK(!contains(read_text(path), "hide_identifiers: false"));
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

static bool presentation_contains(const fs::path& prefix, const std::string& needle) {
    const auto presented = prefix / "var" / "host-identity.presented.yaml";
    if (!fs::exists(presented)) {
        return false;
    }
    return contains(read_text(presented), needle);
}

static void test_session_defaults_and_policies() {
    auto prefix = make_tmpdir("session-default");
    auto session = make_session(prefix);

    CHECK_EQ(inventory_value(session.inventory, "manufacturer").value_or(""),
             std::string("Contoso Computers"));
    CHECK_EQ(inventory_value(session.inventory, "product").value_or(""),
             std::string("ContosoBook 15 G3"));
    CHECK_EQ(std::string(field_mode_name(session.policy.policy_for("manufacturer").mode)), std::string(field_mode_name(FieldMode::Expose)));
    CHECK_EQ(std::string(field_mode_name(session.policy.policy_for("product").mode)), std::string(field_mode_name(FieldMode::Expose)));
    CHECK_EQ(std::string(field_mode_name(session.policy.policy_for("serial_number").mode)), std::string(field_mode_name(FieldMode::Hide)));
    CHECK_EQ(std::string(field_mode_name(session.policy.policy_for("mac_address").mode)), std::string(field_mode_name(FieldMode::Hide)));

    auto saved = session.save();
    CHECK(saved.ok);
    if (!saved.ok) {
        std::cerr << "save error: " << saved.error << "\n";
    }
    CHECK(presentation_contains(prefix, "Contoso Computers"));
    CHECK(presentation_contains(prefix, "ContosoBook 15 G3"));
    CHECK(!presentation_contains(prefix, "SN-C0NT0S0-9F3A-0001"));
    CHECK(!presentation_contains(prefix, "02:11:22:33:44:55"));
    CHECK(fs::exists(prefix / "var" / "host-identity.policy.yaml"));

    // Hide manufacturer, override product, expose serial.
    session.set_field_mode("manufacturer", FieldMode::Hide);
    session.set_field_mode("product", FieldMode::Override, "AdventureBook 14");
    session.set_field_mode("serial_number", FieldMode::Expose);
    saved = session.save();
    CHECK(saved.ok);
    if (!saved.ok) {
        std::cerr << "save error: " << saved.error << "\n";
    }

    session.reload();
    CHECK_EQ(std::string(field_mode_name(session.policy.policy_for("manufacturer").mode)), std::string(field_mode_name(FieldMode::Hide)));
    CHECK_EQ(std::string(field_mode_name(session.policy.policy_for("product").mode)), std::string(field_mode_name(FieldMode::Override)));
    CHECK_EQ(session.policy.policy_for("product").override_value, std::string("AdventureBook 14"));
    CHECK_EQ(std::string(field_mode_name(session.policy.policy_for("serial_number").mode)), std::string(field_mode_name(FieldMode::Expose)));

    const auto presented = read_text(prefix / "var" / "host-identity.presented.yaml");
    CHECK(contains(presented, "AdventureBook 14"));
    CHECK(!contains(presented, "ContosoBook 15 G3"));
    CHECK(!contains(presented, "Contoso Computers"));
    CHECK(contains(presented, "SN-C0NT0S0-9F3A-0001"));

    // Hidden or overridden original values must not leak into advertised files.
    const auto hidden = hidden_identifier_values(session.inventory, session.policy);
    // manufacturer is a model field; still must not appear once hidden.
    CHECK(!contains(presented, "Contoso Computers"));
    CHECK(!contains(presented, "ContosoBook 15 G3"));
    if (fs::exists(prefix / "env" / "host-model.env")) {
        const auto env = read_text(prefix / "env" / "host-model.env");
        CHECK(!contains(env, "ContosoBook 15 G3"));
        CHECK(!contains(env, "Contoso Computers"));
        CHECK(contains(env, "AdventureBook 14"));
    }

    // Restore default: serial leaves the presentation layer.
    session.policy = Policy::defaults();
    saved = session.save();
    CHECK(saved.ok);
    session.reload();
    CHECK_EQ(std::string(field_mode_name(session.policy.policy_for("serial_number").mode)), std::string(field_mode_name(FieldMode::Hide)));
    CHECK(!presentation_contains(prefix, "SN-C0NT0S0-9F3A-0001"));
    CHECK(presentation_contains(prefix, "Contoso Computers"));
    (void)hidden;
}

static void test_policy_yaml_round_trip() {
    Policy p = Policy::defaults();
    p.fields["product"].mode = FieldMode::Override;
    p.fields["product"].override_value = "AdventureBook 14";
    p.fields["serial_number"].mode = FieldMode::Expose;
    auto dir = make_tmpdir("policy-yaml");
    const auto path = dir / "host-identity.policy.yaml";
    p.save_file(path);
    const auto loaded = Policy::load_file(path);
    CHECK_EQ(std::string(field_mode_name(loaded.policy_for("product").mode)), std::string(field_mode_name(FieldMode::Override)));
    CHECK_EQ(loaded.policy_for("product").override_value, std::string("AdventureBook 14"));
    CHECK_EQ(std::string(field_mode_name(loaded.policy_for("serial_number").mode)), std::string(field_mode_name(FieldMode::Expose)));
    CHECK_EQ(std::string(field_mode_name(loaded.policy_for("manufacturer").mode)), std::string(field_mode_name(FieldMode::Expose)));
}

int main() {
    try {
        test_load_example();
        test_round_trip();
        test_refuse_example_and_force_hide();
        test_session_defaults_and_policies();
        test_policy_yaml_round_trip();
    } catch (const std::exception& ex) {
        std::cerr << "EXCEPTION: " << ex.what() << "\n";
        return 1;
    }
    std::cout << "passed " << g_passed << "  failed " << g_failed << "\n";
    return g_failed == 0 ? 0 : 1;
}
