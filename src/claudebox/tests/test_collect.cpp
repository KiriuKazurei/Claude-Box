#include "claudebox/collect.hpp"
#include "claudebox/identity.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

using namespace claudebox::identity;
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

#ifndef CLAUDEBOX_SYS_FIXTURE_DIR
#define CLAUDEBOX_SYS_FIXTURE_DIR ""
#endif
#ifndef CLAUDEBOX_FIXTURE_DIR
#define CLAUDEBOX_FIXTURE_DIR ""
#endif

static std::string read_text(const fs::path& path) {
    std::ifstream in(path);
    return std::string((std::istreambuf_iterator<char>(in)),
                       std::istreambuf_iterator<char>());
}

static bool contains(const std::string& hay, const std::string& needle) {
    return hay.find(needle) != std::string::npos;
}

static fs::path make_tmpdir() {
    auto dir = fs::temp_directory_path() / "claudebox-p2-tests" /
               std::to_string(static_cast<unsigned>(g_passed + g_failed + 1));
    fs::remove_all(dir);
    fs::create_directories(dir);
    return dir;
}

int main() {
    const fs::path sys_root(CLAUDEBOX_SYS_FIXTURE_DIR);
    const fs::path yaml_fix = fs::path(CLAUDEBOX_FIXTURE_DIR) / "host-inventory.raw.yaml";
    if (!fs::is_directory(sys_root)) {
        std::cerr << "FAIL missing sys fixture: " << sys_root << "\n";
        return 1;
    }
    if (!fs::is_regular_file(yaml_fix)) {
        std::cerr << "FAIL missing yaml fixture: " << yaml_fix << "\n";
        return 1;
    }

    auto collected = collect_from_tree(sys_root);
    if (!collected.ok) {
        std::cerr << "FAIL collect_from_tree: " << collected.error << "\n";
        ++g_failed;
    } else {
        ++g_passed;
    }
    CHECK(collected.inventory["manufacturer"] == "Contoso Computers");
    CHECK(collected.inventory["product"] == "ContosoBook 15 G3");
    CHECK(collected.inventory["serial_number"] == "SN-C0NT0S0-9F3A-0001");
    CHECK(collected.inventory["product_uuid"] == "7c9e6679-7425-40de-944b-e07fc1f90ae7");
    CHECK(collected.inventory["board_uuid"] == "6ba7b810-9dad-11d1-80b4-00c04fd430c8");
    CHECK(collected.inventory["chassis_uuid"] == "550e8400-e29b-41d4-a716-446655440000");
    CHECK(collected.inventory["mac_address"] == "02:11:22:33:44:55");
    CHECK(collected.inventory["disk_serial"] == "WD-WCC4N7FIXTURE");
    CHECK(collected.inventory["cpu_id"] == "BFEBFBFF000906EA");
    CHECK(collected.inventory["machine_guid"] == "8d4f3c2a-1b6e-4f90-9c11-deadbeef1234");
    CHECK(collected.inventory["device_instance_path"].find("VEN_8086") != std::string::npos);
    CHECK(collected.inventory["asset_tag"] == "ASSET-FIXTURE-01");
    // Skipped loopback / loop devices
    CHECK(collected.inventory["mac_address"] != "00:00:00:00:00:00");
    CHECK(collected.inventory["disk_serial"] != "LOOP-SHOULD-SKIP");

    auto prefix = make_tmpdir();
    const auto raw_path = raw_inventory_path(prefix);
    write_raw_inventory(raw_path, collected.inventory);
    CHECK(fs::is_regular_file(raw_path));
    const auto raw_text = read_text(raw_path);
    CHECK(contains(raw_text, "NOT the presentation layer"));
    CHECK(contains(raw_text, "SN-C0NT0S0-9F3A-0001"));
    CHECK(contains(raw_text, "02:11:22:33:44:55"));
    CHECK(contains(raw_text, "BFEBFBFF000906EA"));
    // Collect must not create a presentation layer by itself.
    CHECK(!fs::exists(prefix / "presentation"));
    CHECK(!fs::exists(prefix / "env" / "host-model.env"));
    CHECK(!fs::exists(prefix / "var" / "host-identity.presented.yaml"));

    // P1 + P2: default policy publishes model only; raw identifiers stay in raw file.
    auto applied = apply_policy(collected.inventory, Policy::defaults());
    CHECK(applied.ok);
    write_presentation(prefix, applied.presentation);
    auto issues = verify_prefix(collected.inventory, Policy::defaults(), prefix);
    if (!issues.empty()) {
        for (const auto& issue : issues) {
            std::cerr << "  p2 verify: " << issue.message << "\n";
        }
    }
    CHECK(issues.empty());
    const auto presented = read_text(prefix / "var" / "host-identity.presented.yaml");
    const auto env = read_text(prefix / "env" / "host-model.env");
    CHECK(contains(presented, "Contoso Computers"));
    CHECK(contains(presented, "ContosoBook 15 G3"));
    CHECK(!contains(presented, "SN-C0NT0S0-9F3A-0001"));
    CHECK(!contains(env, "SN-C0NT0S0-9F3A-0001"));
    CHECK(!contains(env, "02:11:22:33:44:55"));
    CHECK(!contains(env, "8d4f3c2a-1b6e-4f90-9c11-deadbeef1234"));
    // Raw file still has the identifiers after apply.
    CHECK(contains(read_text(raw_path), "SN-C0NT0S0-9F3A-0001"));

    // Collect from the existing YAML fixture (canonicalized).
    auto from_yaml = collect_from_inventory_file(yaml_fix);
    CHECK(from_yaml.ok);
    CHECK(from_yaml.inventory["manufacturer"] == "Contoso Computers");
    CHECK(from_yaml.inventory["serial_number"] == "SN-C0NT0S0-9F3A-0001");
    CHECK(from_yaml.inventory["mac_address"] == "02:11:22:33:44:55");

    // Missing tree should fail cleanly.
    auto missing = collect_from_tree(prefix / "does-not-exist");
    CHECK(!missing.ok);

    std::cerr << (g_failed == 0 ? "ALL GREEN" : "FAILED") << " (identity collect)  "
              << g_passed << " passed, " << g_failed << " failed\n";
    return g_failed == 0 ? 0 : 1;
}
