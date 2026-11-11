#include "claudebox/collect.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <set>
#include <sstream>
#include <stdexcept>

namespace claudebox::identity {
namespace {

std::string trim_copy(std::string s) {
    auto first = s.find_first_not_of(" \t\r\n");
    auto last = s.find_last_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return {};
    }
    return s.substr(first, last - first + 1);
}

std::string read_trimmed(const std::filesystem::path& path) {
    std::error_code ec;
    if (!std::filesystem::is_regular_file(path, ec)) {
        return {};
    }
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return {};
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    return trim_copy(ss.str());
}

bool is_loopback_mac(std::string_view mac) {
    std::string n;
    for (char c : mac) {
        if (std::isxdigit(static_cast<unsigned char>(c))) {
            n.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        }
    }
    return n.empty() || n == std::string(n.size(), '0');
}

void put(Inventory& inv, const std::string& canonical, const std::string& value) {
    if (value.empty()) {
        return;
    }
    inv[canonical] = value;
}

std::string first_mac(const std::filesystem::path& root) {
    const auto net = root / "sys" / "class" / "net";
    std::error_code ec;
    if (!std::filesystem::is_directory(net, ec)) {
        return {};
    }
    std::vector<std::filesystem::path> ifaces;
    for (const auto& entry : std::filesystem::directory_iterator(net, ec)) {
        ifaces.push_back(entry.path());
    }
    std::sort(ifaces.begin(), ifaces.end());
    for (const auto& iface : ifaces) {
        const auto name = iface.filename().string();
        if (name == "lo" || name.rfind("docker", 0) == 0 || name.rfind("veth", 0) == 0 ||
            name.rfind("br-", 0) == 0) {
            continue;
        }
        const auto mac = read_trimmed(iface / "address");
        if (mac.empty() || is_loopback_mac(mac)) {
            continue;
        }
        return mac;
    }
    return {};
}

std::string first_disk_serial(const std::filesystem::path& root) {
    const auto block = root / "sys" / "block";
    std::error_code ec;
    if (!std::filesystem::is_directory(block, ec)) {
        return {};
    }
    std::vector<std::filesystem::path> disks;
    for (const auto& entry : std::filesystem::directory_iterator(block, ec)) {
        disks.push_back(entry.path());
    }
    std::sort(disks.begin(), disks.end());
    for (const auto& disk : disks) {
        const auto name = disk.filename().string();
        if (name.rfind("loop", 0) == 0 || name.rfind("ram", 0) == 0 ||
            name.rfind("sr", 0) == 0 || name.rfind("dm-", 0) == 0) {
            continue;
        }
        const auto serial = read_trimmed(disk / "device" / "serial");
        if (!serial.empty()) {
            return serial;
        }
    }
    return {};
}

Inventory canonicalize(const Inventory& raw) {
    Inventory out;
    std::set<std::string> seen;
    for (const auto& id : canonical_field_ids()) {
        if (auto val = inventory_value(raw, id)) {
            put(out, id, *val);
            seen.insert(id);
        }
    }
    // Keep unknown keys under their normalized names so a future settings
    // page can still list them. Identifier-like unknowns stay in the raw layer.
    for (const auto& [key, val] : raw) {
        if (val.empty()) {
            continue;
        }
        if (auto id = canonical_field(key)) {
            continue;
        }
        const auto n = normalize_key(key);
        if (n.empty() || seen.count(n) || out.count(n)) {
            continue;
        }
        out[n] = val;
        seen.insert(n);
    }
    return out;
}

}  // namespace

std::filesystem::path raw_inventory_path(const std::filesystem::path& prefix) {
    return prefix / "var" / "host-inventory.raw.yaml";
}

void write_raw_inventory(const std::filesystem::path& path, const Inventory& inventory) {
    std::filesystem::create_directories(path.parent_path());
    std::ostringstream body;
    body << "# Claude Box raw host inventory. This is NOT the presentation layer.\n"
         << "# Identifiers stay here until the user authorizes a field in settings.\n"
         << "# Do not commit real serials, UUIDs, MACs, or other hardware encodings.\n";
    auto write_id = [&](const std::string& id) {
        auto it = inventory.find(id);
        if (it != inventory.end()) {
            body << id << ": " << yaml_quote(it->second) << "\n";
        }
    };
    for (const auto& id : canonical_field_ids()) {
        write_id(id);
    }
    for (const auto& [id, val] : inventory) {
        bool known = false;
        for (const auto& k : canonical_field_ids()) {
            if (k == id) {
                known = true;
                break;
            }
        }
        if (!known) {
            body << id << ": " << yaml_quote(val) << "\n";
        }
    }
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        throw std::runtime_error("cannot write raw inventory: " + path.string());
    }
    out << body.str();
}

CollectResult collect_from_tree(const std::filesystem::path& source_root) {
    CollectResult result;
    std::error_code ec;
    if (!std::filesystem::is_directory(source_root, ec)) {
        result.error = "source root is not a directory: " + source_root.string();
        return result;
    }
    const auto dmi = source_root / "sys" / "class" / "dmi" / "id";
    Inventory raw;
    put(raw, "manufacturer", read_trimmed(dmi / "sys_vendor"));
    put(raw, "product", read_trimmed(dmi / "product_name"));
    put(raw, "serial_number", read_trimmed(dmi / "product_serial"));
    put(raw, "product_uuid", read_trimmed(dmi / "product_uuid"));
    put(raw, "board_uuid", read_trimmed(dmi / "board_uuid"));
    put(raw, "chassis_uuid", read_trimmed(dmi / "chassis_uuid"));
    put(raw, "asset_tag", read_trimmed(dmi / "chassis_asset_tag"));
    put(raw, "mac_address", first_mac(source_root));
    put(raw, "disk_serial", first_disk_serial(source_root));

    const auto overlay = source_root / "claudebox";
    put(raw, "cpu_id", read_trimmed(overlay / "cpu_id"));
    put(raw, "machine_guid", read_trimmed(overlay / "machine_guid"));
    put(raw, "device_instance_path", read_trimmed(overlay / "device_instance_path"));

    result.inventory = canonicalize(raw);
    if (result.inventory.find("manufacturer") == result.inventory.end() &&
        result.inventory.find("product") == result.inventory.end()) {
        result.error = "source tree has no manufacturer or product";
        return result;
    }
    result.ok = true;
    return result;
}

CollectResult collect_from_inventory_file(const std::filesystem::path& path) {
    CollectResult result;
    try {
        const auto raw = load_inventory_file(path);
        result.inventory = canonicalize(raw);
        if (result.inventory.empty()) {
            result.error = "inventory file produced no fields: " + path.string();
            return result;
        }
        result.ok = true;
        return result;
    } catch (const std::exception& ex) {
        result.error = ex.what();
        return result;
    }
}

}  // namespace claudebox::identity
