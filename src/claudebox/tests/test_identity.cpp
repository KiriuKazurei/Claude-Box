#include "claudebox/identity.hpp"
#include <cstdlib>

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

#ifndef CLAUDEBOX_FIXTURE_DIR
#define CLAUDEBOX_FIXTURE_DIR ""
#endif

static fs::path fixture_path() {
    const char* env = std::getenv("CLAUDEBOX_FIXTURE");
    if (env && *env) {
        return fs::path(env);
    }
    fs::path from_cmake(CLAUDEBOX_FIXTURE_DIR);
    if (!from_cmake.empty()) {
        return from_cmake / "host-inventory.raw.yaml";
    }
    return fs::path("../tests/wsl-box/fixtures/host-inventory.raw.yaml");
}

static fs::path make_tmpdir() {
    auto dir = fs::temp_directory_path() / "claudebox-p1-tests" /
               std::to_string(std::hash<std::string>{}(__FILE__ + std::to_string(g_passed + g_failed)));
    fs::remove_all(dir);
    fs::create_directories(dir);
    return dir;
}

static void write_text(const fs::path& path, std::string_view text) {
    fs::create_directories(path.parent_path());
    std::ofstream out(path);
    out << text;
}

static std::string read_text(const fs::path& path) {
    std::ifstream in(path);
    return std::string((std::istreambuf_iterator<char>(in)),
                       std::istreambuf_iterator<char>());
}

static bool contains(const std::string& hay, const std::string& needle) {
    return hay.find(needle) != std::string::npos;
}

int main() {
    CHECK_EQ(normalize_key("SerialNumber"), "serialnumber");
    CHECK(canonical_field("SerialNumber") == "serial_number");
    CHECK_EQ(normalize_key("Win32_ComputerSystem.Model"), "win32_computersystem_model");
    CHECK(canonical_field("Manufacturer") == "manufacturer");
    CHECK(canonical_field("Model") == "product");
    CHECK(canonical_field("serial") == "serial_number");
    CHECK(canonical_field("mac_address") == "mac_address");
    CHECK(classify_key("product") == FieldKind::Model);
    CHECK(classify_key("cpu_id") == FieldKind::Identifier);
    CHECK(classify_key("mystery_uuid") == FieldKind::Identifier);
    CHECK(looks_like_identifier("550e8400-e29b-41d4-a716-446655440000") == "uuid-or-guid");
    CHECK(looks_like_identifier("02:11:22:33:44:55") == "mac-address");
    CHECK(!looks_like_identifier("ContosoBook 15 G3"));

    const auto fixture = fixture_path();
    if (!fs::is_regular_file(fixture)) {
        std::cerr << "FAIL missing fixture: " << fixture << "\n";
        return 1;
    }
    const Inventory inv = load_inventory_file(fixture);
    CHECK(inventory_value(inv, "manufacturer") == "Contoso Computers");
    CHECK(inventory_value(inv, "product") == "ContosoBook 15 G3");
    CHECK(inventory_value(inv, "serial_number") == "SN-C0NT0S0-9F3A-0001");
    CHECK(inventory_value(inv, "mac_address") == "02:11:22:33:44:55");
    CHECK(inventory_value(inv, "machine_guid") == "8d4f3c2a-1b6e-4f90-9c11-deadbeef1234");

    const Policy def = Policy::defaults();
    CHECK(def.policy_for("manufacturer").mode == FieldMode::Expose);
    CHECK(def.policy_for("product").mode == FieldMode::Expose);
    CHECK(def.policy_for("serial_number").mode == FieldMode::Hide);
    CHECK(def.policy_for("mac_address").mode == FieldMode::Hide);
    CHECK(def.policy_for("cpu_id").mode == FieldMode::Hide);
    CHECK(def.policy_for("machine_guid").mode == FieldMode::Hide);
    CHECK(def.policy_for("device_instance_path").mode == FieldMode::Hide);

    auto applied = apply_policy(inv, def);
    CHECK(applied.ok);
    CHECK(applied.presentation.fields["manufacturer"] == "Contoso Computers");
    CHECK(applied.presentation.fields["product"] == "ContosoBook 15 G3");
    CHECK(!applied.presentation.fields.count("serial_number"));
    CHECK(!applied.presentation.fields.count("mac_address"));
    CHECK(!applied.presentation.fields.count("product_uuid"));
    CHECK(!applied.presentation.fields.count("machine_guid"));
    auto mem_issues = verify(inv, def, applied.presentation);
    CHECK(mem_issues.empty());

    auto prefix = make_tmpdir();
    write_presentation(prefix, applied.presentation);
    // Raw inventory may sit next to the presentation; it must not be treated as advertised.
    fs::copy_file(fixture, prefix / "var" / "host-inventory.raw.yaml");
    auto prefix_issues = verify_prefix(inv, def, prefix);
    if (!prefix_issues.empty()) {
        for (const auto& issue : prefix_issues) {
            std::cerr << "  default prefix: " << issue.message << "\n";
        }
    }
    CHECK(prefix_issues.empty());
    const auto env = read_text(prefix / "env" / "host-model.env");
    CHECK(contains(env, "Contoso Computers"));
    CHECK(contains(env, "ContosoBook 15 G3"));
    CHECK(!contains(env, "SN-C0NT0S0-9F3A-0001"));
    CHECK(!contains(env, "02:11:22:33:44:55"));
    CHECK(!contains(env, "BFEBFBFF000906EA"));
    CHECK(!contains(env, "8d4f3c2a-1b6e-4f90-9c11-deadbeef1234"));
    CHECK(!contains(env, "WD-WCC4N7FIXTURE"));
    CHECK(!fs::exists(prefix / "presentation" / "dmi" / "id" / "product_serial"));
    CHECK(read_text(prefix / "presentation" / "dmi" / "id" / "sys_vendor").find("Contoso Computers") == 0);

    // Planting a serial into the presentation layer must fail verify.
    write_text(prefix / "presentation" / "dmi" / "id" / "product_serial",
               "SN-C0NT0S0-9F3A-0001\n");
    auto planted = verify_prefix(inv, def, prefix);
    CHECK(!planted.empty());
    fs::remove(prefix / "presentation" / "dmi" / "id" / "product_serial");

    // Stale identifier DMI files must be removed on the next apply.
    write_text(prefix / "presentation" / "dmi" / "id" / "product_serial", "STALE\n");
    write_presentation(prefix, applied.presentation);
    CHECK(!fs::exists(prefix / "presentation" / "dmi" / "id" / "product_serial"));

    // User exposes a serial: it may enter the presentation layer.
    Policy expose_serial = def;
    expose_serial.fields["serial_number"].mode = FieldMode::Expose;
    auto ex = apply_policy(inv, expose_serial);
    CHECK(ex.ok);
    CHECK(ex.presentation.fields["serial_number"] == "SN-C0NT0S0-9F3A-0001");
    CHECK(!ex.presentation.fields.count("mac_address"));
    auto ex_issues = verify(inv, expose_serial, ex.presentation);
    CHECK(ex_issues.empty());
    auto ex_dir = make_tmpdir();
    write_presentation(ex_dir, ex.presentation);
    auto ex_prefix = verify_prefix(inv, expose_serial, ex_dir);
    CHECK(ex_prefix.empty());
    CHECK(contains(read_text(ex_dir / "var" / "host-identity.presented.yaml"),
                   "SN-C0NT0S0-9F3A-0001"));
    CHECK(!contains(read_text(ex_dir / "var" / "host-identity.presented.yaml"),
                    "02:11:22:33:44:55"));
    CHECK(fs::exists(ex_dir / "presentation" / "dmi" / "id" / "product_serial"));

    // Hide manufacturer: original vendor string must leave the presentation layer.
    Policy hide_vendor = def;
    hide_vendor.fields["manufacturer"].mode = FieldMode::Hide;
    auto hv = apply_policy(inv, hide_vendor);
    CHECK(hv.ok);
    CHECK(!hv.presentation.fields.count("manufacturer"));
    CHECK(hv.presentation.fields["product"] == "ContosoBook 15 G3");
    CHECK(verify(inv, hide_vendor, hv.presentation).empty());
    auto hv_dir = make_tmpdir();
    write_presentation(hv_dir, hv.presentation);
    const auto hv_text = read_text(hv_dir / "env" / "host-model.env") +
                         read_text(hv_dir / "var" / "host-identity.presented.yaml");
    CHECK(!contains(hv_text, "Contoso Computers"));
    CHECK(verify_prefix(inv, hide_vendor, hv_dir).empty());

    // Override product: new text is shown, original model must not leak.
    Policy over = def;
    over.fields["product"].mode = FieldMode::Override;
    over.fields["product"].override_value = "Renamed Book";
    auto ov = apply_policy(inv, over);
    CHECK(ov.ok);
    CHECK(ov.presentation.fields["product"] == "Renamed Book");
    CHECK(verify(inv, over, ov.presentation).empty());
    auto ov_dir = make_tmpdir();
    write_presentation(ov_dir, ov.presentation);
    const auto ov_all = read_text(ov_dir / "env" / "host-model.env") +
                        read_text(ov_dir / "var" / "host-identity.presented.yaml") +
                        read_text(ov_dir / "presentation" / "dmi" / "id" / "product_name");
    CHECK(contains(ov_all, "Renamed Book"));
    CHECK(!contains(ov_all, "ContosoBook 15 G3"));
    CHECK(verify_prefix(inv, over, ov_dir).empty());

    // Override an identifier: fake value is published, real one must not leak.
    Policy fake_mac = def;
    fake_mac.fields["mac_address"].mode = FieldMode::Override;
    fake_mac.fields["mac_address"].override_value = "00-11-22-AA-BB-CC";
    auto fm = apply_policy(inv, fake_mac);
    CHECK(fm.ok);
    CHECK(fm.presentation.fields["mac_address"] == "00-11-22-AA-BB-CC");
    CHECK(verify(inv, fake_mac, fm.presentation).empty());
    auto fm_dir = make_tmpdir();
    write_presentation(fm_dir, fm.presentation);
    const auto fm_text = read_text(fm_dir / "env" / "host-model.env") +
                         read_text(fm_dir / "var" / "host-identity.presented.yaml");
    CHECK(contains(fm_text, "00-11-22-AA-BB-CC"));
    CHECK(!contains(fm_text, "02:11:22:33:44:55"));
    CHECK(verify_prefix(inv, fake_mac, fm_dir).empty());

    // Mis-labelled UUID as manufacturer must be refused.
    Inventory bad = inv;
    bad["manufacturer"] = "550e8400-e29b-41d4-a716-446655440000";
    bad["Manufacturer"] = "550e8400-e29b-41d4-a716-446655440000";
    auto refused = apply_policy(bad, def);
    CHECK(!refused.ok);
    CHECK(contains(refused.error, "uuid-or-guid"));

    // Policy YAML parsing.
    const auto parsed = Policy::parse_yaml(R"(
fields:
  manufacturer:
    mode: hide
  product:
    mode: override
    value: "Custom Model"
  serial_number:
    mode: expose
)");
    CHECK(parsed.policy_for("manufacturer").mode == FieldMode::Hide);
    CHECK(parsed.policy_for("product").mode == FieldMode::Override);
    CHECK_EQ(parsed.policy_for("product").override_value, "Custom Model");
    CHECK(parsed.policy_for("serial_number").mode == FieldMode::Expose);
    CHECK(parsed.policy_for("cpu_id").mode == FieldMode::Hide);  // default kept

    auto py = apply_policy(inv, parsed);
    CHECK(py.ok);
    CHECK(!py.presentation.fields.count("manufacturer"));
    CHECK(py.presentation.fields["product"] == "Custom Model");
    CHECK(py.presentation.fields["serial_number"] == "SN-C0NT0S0-9F3A-0001");
    CHECK(verify(inv, parsed, py.presentation).empty());

    std::cerr << (g_failed == 0 ? "ALL GREEN" : "FAILED") << " (identity engine)  "
              << g_passed << " passed, " << g_failed << " failed\n";
    return g_failed == 0 ? 0 : 1;
}
