#include "claudebox/config.hpp"
#include "claudebox/vault.hpp"

#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <unistd.h>

using claudebox::config::BoxConfig;
using claudebox::gateway::BoxProcess;
using claudebox::vault::ActionOptions;
using claudebox::vault::archive;
using claudebox::vault::default_vault_dir;
using claudebox::vault::format_status;
using claudebox::vault::is_allowed_vault_dir;
using claudebox::vault::is_forbidden_host_path;
using claudebox::vault::is_vault_close_target;
using claudebox::vault::kCipherName;
using claudebox::vault::kKdfName;
using claudebox::vault::list_snapshots;
using claudebox::vault::plan;
using claudebox::vault::rollback;
using claudebox::vault::switch_to;

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

static void write_text(const fs::path& path, const std::string& body) {
    fs::create_directories(path.parent_path());
    std::ofstream out(path);
    out << body;
}

static bool contains(const std::string& hay, const std::string& needle) {
    return hay.find(needle) != std::string::npos;
}

static fs::path unique_prefix(const std::string& tag) {
    const auto base = fs::temp_directory_path() / "claudebox-p9" /
                      (tag + "-" + std::to_string(::getpid()) + "-" +
                       std::to_string(static_cast<long>(std::time(nullptr))));
    fs::remove_all(base);
    fs::create_directories(base);
    return base;
}

static BoxConfig load_example() { return BoxConfig::load_file(example_config()); }

static const char* kPass = "fixture-passphrase-not-for-logs";

static void seed_identity(const fs::path& workspace, const fs::path& companion,
                          const std::string& name, const std::string& token) {
    write_text(workspace / "identity.json",
               std::string("{\"kind\":\"claude-box-identity\",\"name\":\"") + name + "\"}\n");
    write_text(workspace / "desktop" / "settings.json", "{\"theme\":\"dark\"}\n");
    write_text(workspace / "desktop" / "login.json",
               std::string("{\"token\":\"") + token + "\"}\n");
    write_text(workspace / "companion-bind.txt", companion.string() + "\n");
    write_text(companion / "Preferences", std::string("{\"profile\":\"") + name + "\"}\n");
}

static bool written_outside_box(const claudebox::vault::ActionResult& result,
                                const fs::path& prefix) {
    if (result.wrote_outside_box) return true;
    for (const auto& p : result.written) {
        const auto s = p.string();
        if (s.find("AppData") != std::string::npos && s.find("ClaudeBox") == std::string::npos) {
            return true;
        }
        if (s.find("/etc/localtime") != std::string::npos) return true;
        auto cur = p;
        bool under = false;
        for (int i = 0; i < 16 && !cur.empty(); ++i) {
            if (cur == prefix) {
                under = true;
                break;
            }
            if (!cur.has_parent_path() || cur.parent_path() == cur) break;
            cur = cur.parent_path();
        }
        if (!under) return true;
    }
    return false;
}

static void test_path_policy() {
    const auto prefix = unique_prefix("paths");
    CHECK(is_forbidden_host_path("/mnt/c/Users/zero/AppData/Roaming/Claude"));
    CHECK(is_forbidden_host_path("/mnt/c/Users/zero/AppData/Local/Google/Chrome/User Data"));
    CHECK(!is_forbidden_host_path(default_vault_dir(prefix)));
    std::string err;
    CHECK(is_allowed_vault_dir(default_vault_dir(prefix), prefix, &err));
    CHECK(!is_allowed_vault_dir("/mnt/c/Users/zero/AppData/Roaming/Claude", prefix, &err));
    CHECK(is_vault_close_target("claude-desktop"));
    CHECK(is_vault_close_target("companion-browser"));
    CHECK(!is_vault_close_target("firefox"));
    CHECK(!is_vault_close_target("gost"));
    CHECK(!is_vault_close_target("google-chrome"));
    fs::remove_all(prefix);
}

static void test_archive_switch_empty_and_back() {
    const auto prefix = unique_prefix("roundtrip");
    auto cfg = load_example();
    CHECK(cfg.features.identity_vault);
    const auto vault = default_vault_dir(prefix);
    const auto workspace = vault / "workspace";
    const auto companion = prefix / "var" / "companion-browser" / "identities" / "live" / "profile";
    seed_identity(workspace, companion, "fixture-a", "token-aaa");

    ActionOptions opts;
    opts.label = "fixture-a";
    opts.processes = {
        {"claude-desktop", 101, true},
        {"companion-browser", 202, true},
        {"firefox", 303, true},
        {"gost", 404, true},
    };

    const auto planned = plan(cfg, prefix, opts);
    CHECK(planned.ok);
    CHECK(planned.close.starts_polite_close);
    CHECK_EQ(planned.close.targets.size(), size_t{2});
    const auto status = format_status(planned);
    CHECK(contains(status, "AES-256-GCM"));
    CHECK(contains(status, kKdfName) || contains(status, "PBKDF2"));
    CHECK(!contains(status, kPass));
    CHECK(contains(status, "不会改宿主"));

    const auto archived = archive(cfg, prefix, kPass, opts);
    CHECK(archived.ok);
    CHECK(!written_outside_box(archived, prefix));
    CHECK(!contains(archived.error, kPass));
    CHECK(!archived.current_id.empty());
    CHECK(fs::exists(vault / "generations" / archived.current_id / "archive.enc"));
    // ciphertext must not contain the fixture token
    const auto enc = read_text(vault / "generations" / archived.current_id / "archive.enc");
    CHECK(!contains(enc, "token-aaa"));
    CHECK(contains(read_text(workspace / "desktop" / "login.json"), "token-aaa"));

    const auto to_empty = switch_to(cfg, prefix, "empty", kPass, opts);
    CHECK(to_empty.ok);
    CHECK(!written_outside_box(to_empty, prefix));
    CHECK(contains(read_text(workspace / "identity.json"), "empty"));
    CHECK(!contains(read_text(workspace / "identity.json"), "fixture-a"));
    CHECK(!fs::exists(workspace / "desktop" / "login.json") ||
          !contains(read_text(workspace / "desktop" / "login.json"), "token-aaa"));

    const auto snaps = list_snapshots(to_empty.plan.paths);
    CHECK(snaps.size() >= size_t{2});
    std::string first_id = archived.current_id;
    const auto back = switch_to(cfg, prefix, first_id, kPass, opts);
    CHECK(back.ok);
    CHECK(!written_outside_box(back, prefix));
    CHECK(contains(read_text(workspace / "identity.json"), "fixture-a"));
    CHECK(contains(read_text(workspace / "desktop" / "login.json"), "token-aaa"));
    CHECK(contains(read_text(workspace / "desktop" / "settings.json"), "dark"));
    fs::remove_all(prefix);
}

static void test_forced_failure_rolls_back() {
    const auto prefix = unique_prefix("rollback");
    auto cfg = load_example();
    const auto workspace = default_vault_dir(prefix) / "workspace";
    const auto companion = prefix / "var" / "companion-browser" / "identities" / "live" / "profile";
    seed_identity(workspace, companion, "fixture-a", "token-aaa");

    ActionOptions opts;
    opts.label = "fixture-a";
    const auto archived = archive(cfg, prefix, kPass, opts);
    CHECK(archived.ok);
    const auto before_id = archived.current_id;
    const auto before_login = read_text(workspace / "desktop" / "login.json");

    ActionOptions fail = opts;
    fail.force_fail_after = "after_verify";
    fail.label = "should-not-commit";
    const auto failed = switch_to(cfg, prefix, "empty", kPass, fail);
    CHECK(!failed.ok);
    CHECK(contains(failed.error, "forced failure"));
    CHECK(!contains(failed.error, kPass));
    // A safety archive may have been written first; the failed empty
    // generation must not become current, and the live workspace must
    // still be fixture-a.
    CHECK_EQ(read_text(workspace / "desktop" / "login.json"), before_login);
    CHECK(contains(read_text(workspace / "identity.json"), "fixture-a"));
    const auto snaps = list_snapshots(plan(cfg, prefix).paths);
    bool saw_current = false;
    for (const auto& s : snaps) {
        if (s.current) {
            saw_current = true;
            CHECK(s.label != "should-not-commit");
            CHECK(s.label != "empty");
            CHECK(s.id == before_id || s.label == "pre-switch");
        }
        if (s.label == "should-not-commit") {
            CHECK(!s.current);
        }
    }
    CHECK(saw_current);
    CHECK(contains(format_status(plan(cfg, prefix)), "fixture-a") ||
          contains(format_status(plan(cfg, prefix)), before_id) ||
          contains(format_status(plan(cfg, prefix)), "pre-switch"));
    fs::remove_all(prefix);
}

static void test_explicit_rollback() {
    const auto prefix = unique_prefix("explicit-rollback");
    auto cfg = load_example();
    const auto workspace = default_vault_dir(prefix) / "workspace";
    const auto companion = prefix / "var" / "companion-browser" / "identities" / "live" / "profile";
    seed_identity(workspace, companion, "fixture-a", "token-aaa");
    ActionOptions opts;
    const auto first = archive(cfg, prefix, kPass, opts);
    CHECK(first.ok);
    const auto emptied = switch_to(cfg, prefix, "empty", kPass, opts);
    CHECK(emptied.ok);
    CHECK(contains(read_text(workspace / "identity.json"), "empty"));
    const auto rolled = rollback(cfg, prefix, kPass, opts);
    CHECK(rolled.ok);
    CHECK(rolled.rolled_back);
    CHECK(contains(read_text(workspace / "desktop" / "login.json"), "token-aaa") ||
          contains(read_text(workspace / "identity.json"), "fixture-a") ||
          contains(read_text(workspace / "identity.json"), "empty") == false);
    // After switch to empty, previous should be the pre-switch archive of fixture-a
    // or the original archive; rollback should not leave host files.
    CHECK(!written_outside_box(rolled, prefix));
    fs::remove_all(prefix);
}

static void test_wrong_passphrase_and_disabled() {
    const auto prefix = unique_prefix("auth");
    auto cfg = load_example();
    const auto workspace = default_vault_dir(prefix) / "workspace";
    const auto companion = prefix / "var" / "companion-browser" / "identities" / "live" / "profile";
    seed_identity(workspace, companion, "fixture-a", "token-aaa");
    ActionOptions opts;
    const auto archived = archive(cfg, prefix, kPass, opts);
    CHECK(archived.ok);
    const auto bad = switch_to(cfg, prefix, archived.current_id, "wrong-passphrase", opts);
    CHECK(!bad.ok);
    CHECK(!contains(bad.error, "wrong-passphrase"));
    CHECK(!contains(bad.error, kPass));
    CHECK(contains(read_text(workspace / "desktop" / "login.json"), "token-aaa"));

    cfg.features.identity_vault = false;
    const auto denied = archive(cfg, prefix, kPass, opts);
    CHECK(!denied.ok);
    CHECK(contains(denied.error, "disabled") || contains(denied.error, "identity_vault"));
    fs::remove_all(prefix);
}

static void test_passphrase_file_env() {
    const auto prefix = unique_prefix("passfile");
    const auto passfile = prefix / "secret" / "pass.txt";
    write_text(passfile, std::string(kPass) + "\n");
    setenv("CLAUDE_BOX_VAULT_PASSPHRASE_FILE", passfile.c_str(), 1);
    unsetenv("CLAUDE_BOX_VAULT_PASSPHRASE");
    std::string loaded, err;
    CHECK(claudebox::vault::load_passphrase(&loaded, &err));
    CHECK_EQ(loaded, std::string(kPass));
    unsetenv("CLAUDE_BOX_VAULT_PASSPHRASE_FILE");
    fs::remove_all(prefix);
}

static void test_rejects_host_vault_dir() {
    const auto prefix = unique_prefix("host-dir");
    auto cfg = load_example();
    cfg.features.vault_dir = "/mnt/c/Users/zero/AppData/Roaming/Claude";
    const auto p = plan(cfg, prefix);
    CHECK(!p.ok);
    bool refused = false;
    for (const auto& e : p.errors) {
        if (contains(e, "Claude") || contains(e, "host")) refused = true;
    }
    CHECK(refused);
    fs::remove_all(prefix);
}

int main() {
    try {
        test_path_policy();
        test_archive_switch_empty_and_back();
        test_forced_failure_rolls_back();
        test_explicit_rollback();
        test_wrong_passphrase_and_disabled();
        test_passphrase_file_env();
        test_rejects_host_vault_dir();
    } catch (const std::exception& ex) {
        std::cerr << "EXCEPTION " << ex.what() << "\n";
        return 1;
    }
    std::cout << "passed=" << g_passed << " failed=" << g_failed << "\n";
    return g_failed == 0 ? 0 : 1;
}
