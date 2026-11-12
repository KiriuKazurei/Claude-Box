#include "claudebox/config.hpp"
#include "claudebox/mount.hpp"
#include "claudebox/settings.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

using claudebox::config::BoxConfig;
using claudebox::config::VisibleDir;
using claudebox::mount::format_plan;
using claudebox::mount::is_allowed_guest_prefix;
using claudebox::mount::is_forbidden_host_path;
using claudebox::mount::is_host_c_guest_path;
using claudebox::mount::is_system_drive_root;
using claudebox::mount::is_whole_users_tree;
using claudebox::mount::plan_mounts;
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

static std::string read_text(const fs::path& path) {
    std::ifstream in(path);
    return std::string((std::istreambuf_iterator<char>(in)),
                       std::istreambuf_iterator<char>());
}

static bool contains(const std::string& hay, const std::string& needle) {
    return hay.find(needle) != std::string::npos;
}

static void test_classifiers() {
    for (const auto* p : {"C:", "C:\\", "C:/", "c:", "c:\\", "/mnt/c", "/mnt/c/", "/mnt/C"}) {
        CHECK(is_system_drive_root(p));
        CHECK(is_forbidden_host_path(p));
    }
    CHECK(!is_system_drive_root("D:\\Projects\\demo"));
    CHECK(!is_system_drive_root("C:\\Windows"));
    CHECK(!is_forbidden_host_path("D:\\Projects\\demo"));

    for (const auto* p : {"C:\\Users", "C:\\Users\\", "C:/Users", "C:\\Users\\Alice",
                          "C:/Users/Alice/", "/mnt/c/Users", "/mnt/c/Users/Alice"}) {
        CHECK(is_whole_users_tree(p));
        CHECK(is_forbidden_host_path(p));
        CHECK(!is_system_drive_root(p));
    }
    CHECK(!is_whole_users_tree("C:\\Users\\Alice\\Documents"));
    CHECK(!is_forbidden_host_path("C:\\Users\\Alice\\Documents"));

    CHECK(is_host_c_guest_path("/mnt/c"));
    CHECK(is_host_c_guest_path("/mnt/c/"));
    CHECK(is_host_c_guest_path("/mnt/c/Users"));
    CHECK(!is_host_c_guest_path("/mnt/claude-box/demo"));

    CHECK(is_allowed_guest_prefix("/mnt/claude-box"));
    CHECK(is_allowed_guest_prefix("/mnt/claude-box/"));
    CHECK(is_allowed_guest_prefix("/mnt/claude-box/demo"));
    CHECK(!is_allowed_guest_prefix("/mnt/c"));
    CHECK(!is_allowed_guest_prefix("/mnt/d"));
    CHECK(!is_allowed_guest_prefix("/home/demo"));
    CHECK(!is_allowed_guest_prefix("/mnt/claude-box/../c"));
}

static void test_example_plan_succeeds() {
    const auto cfg = BoxConfig::load_file(example_config());
    const auto plan = plan_mounts(cfg);
    if (!plan.ok) {
        for (const auto& err : plan.errors) {
            std::cerr << "example plan error: " << err << "\n";
        }
    }
    CHECK(plan.ok);
    CHECK(plan.disable_automount);
    CHECK(plan.exclude_host_system_drive);
    CHECK_EQ(plan.mounts.size(), size_t{1});
    CHECK_EQ(plan.mounts[0].host_path, std::string("D:\\Projects\\demo"));
    CHECK_EQ(plan.mounts[0].guest_path, std::string("/mnt/claude-box/demo"));
    CHECK(!plan.mounts[0].read_only);
    CHECK(!plan.distro.steps.empty());
    const auto text = format_plan(plan);
    CHECK(contains(text, "D:\\Projects\\demo"));
    CHECK(contains(text, "/mnt/claude-box/demo"));
    CHECK(contains(text, "不会调用 wsl.exe"));
    CHECK(contains(text, "automount.enabled=false"));
    CHECK(contains(text, "校验通过"));
}

static BoxConfig base_cfg() {
    auto cfg = BoxConfig::defaults();
    cfg.visible_dirs = {VisibleDir{"D:\\Projects\\demo", "/mnt/claude-box/demo", false}};
    cfg.features.exclude_host_system_drive = true;
    cfg.mount.disable_automount = true;
    return cfg;
}

static void expect_reject(const BoxConfig& cfg, const std::string& needle) {
    const auto plan = plan_mounts(cfg);
    CHECK(!plan.ok);
    bool found = false;
    for (const auto& err : plan.errors) {
        if (contains(err, needle)) {
            found = true;
        }
    }
    if (!found) {
        std::cerr << "FAIL expected error containing: " << needle << "\n";
        for (const auto& err : plan.errors) {
            std::cerr << "  got: " << err << "\n";
        }
        ++g_failed;
    } else {
        ++g_passed;
    }
}

static void test_rejects_forbidden_dirs() {
    const std::vector<std::string> roots = {"C:", "C:\\", "C:/", "c:\\", "/mnt/c", "/mnt/c/"};
    for (const auto& host : roots) {
        auto cfg = base_cfg();
        cfg.visible_dirs = {VisibleDir{host, "/mnt/claude-box/system", false}};
        expect_reject(cfg, "系统盘");
    }
    for (const auto& host :
         std::vector<std::string>{"C:\\Users", "C:\\Users\\", "C:/Users", "C:\\Users\\Alice",
                                  "/mnt/c/Users"}) {
        auto cfg = base_cfg();
        cfg.visible_dirs = {VisibleDir{host, "/mnt/claude-box/users", false}};
        expect_reject(cfg, "Users");
    }
    {
        auto cfg = base_cfg();
        cfg.visible_dirs = {VisibleDir{"D:\\Projects\\demo", "/mnt/c", false}};
        expect_reject(cfg, "C:");
    }
    {
        auto cfg = base_cfg();
        cfg.visible_dirs = {VisibleDir{"D:\\Projects\\demo", "/mnt/c/Users", false}};
        expect_reject(cfg, "C:");
    }
    {
        auto cfg = base_cfg();
        cfg.visible_dirs = {VisibleDir{"D:\\Projects\\demo", "/home/demo", false}};
        expect_reject(cfg, "/mnt/claude-box");
    }
    {
        auto cfg = base_cfg();
        cfg.features.exclude_host_system_drive = false;
        expect_reject(cfg, "exclude_host_system_drive");
    }
    {
        auto cfg = base_cfg();
        cfg.mount.disable_automount = false;
        expect_reject(cfg, "disable_automount");
    }
}

static void test_session_save_rejects_and_does_not_write() {
    auto dir = fs::temp_directory_path() / "claudebox-p5-tests" / "save-reject";
    fs::remove_all(dir);
    fs::create_directories(dir / "config");
    const auto cfg_path = dir / "config" / "wsl-box.yaml";
    BoxConfig::defaults().save_file(cfg_path);
    const auto before = read_text(cfg_path);

    claudebox::settings::Options opt;
    opt.prefix = dir;
    opt.config = cfg_path;
    auto session = Session::load(opt);
    session.config.visible_dirs = {VisibleDir{"C:\\", "/mnt/claude-box/root", false}};
    const auto saved = session.save();
    CHECK(!saved.ok);
    CHECK(contains(saved.error, "系统盘") || contains(saved.error, "C:"));
    CHECK_EQ(read_text(cfg_path), before);
    CHECK(!fs::exists(dir / "var" / "host-identity.policy.yaml"));
}

int main() {
    try {
        test_classifiers();
        test_example_plan_succeeds();
        test_rejects_forbidden_dirs();
        test_session_save_rejects_and_does_not_write();
    } catch (const std::exception& ex) {
        std::cerr << "EXCEPTION: " << ex.what() << "\n";
        return 1;
    }
    std::cout << "passed " << g_passed << "  failed " << g_failed << "\n";
    return g_failed == 0 ? 0 : 1;
}
