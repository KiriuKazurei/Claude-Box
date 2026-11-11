#include "claudebox/identity.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <regex>
#include <set>
#include <sstream>
#include <system_error>

namespace claudebox::identity {
namespace {

struct FieldSpec {
    const char* id;
    FieldKind kind;
    std::vector<const char*> aliases;
};

const std::vector<FieldSpec>& specs() {
    static const std::vector<FieldSpec> k = {
        {"manufacturer", FieldKind::Model,
         {"manufacturer", "sys_vendor", "vendor", "system_manufacturer",
          "win32_computersystem_manufacturer", "win32_computersystem.manufacturer"}},
        {"product", FieldKind::Model,
         {"product", "product_name", "productname", "model", "system_product_name",
          "win32_computersystem_model", "win32_computersystem.model"}},
        {"serial_number", FieldKind::Identifier,
         {"serial", "serial_number", "serialnumber", "product_serial", "board_serial",
          "chassis_serial", "bios_serial"}},
        {"chassis_uuid", FieldKind::Identifier, {"chassis_uuid"}},
        {"board_uuid", FieldKind::Identifier, {"board_uuid"}},
        {"product_uuid", FieldKind::Identifier,
         {"product_uuid", "uuid", "system_uuid", "machine_uuid"}},
        {"mac_address", FieldKind::Identifier, {"mac", "mac_address", "macaddress"}},
        {"disk_serial", FieldKind::Identifier, {"disk_serial", "volume_serial"}},
        {"cpu_id", FieldKind::Identifier, {"cpu_id", "processor_id", "processorid"}},
        {"machine_guid", FieldKind::Identifier, {"machine_guid", "machineguid"}},
        {"device_instance_path", FieldKind::Identifier,
         {"device_instance_path", "pnp_device_id", "pnpdeviceid", "instancename"}},
        {"asset_tag", FieldKind::Identifier,
         {"asset_tag", "board_asset_tag", "chassis_asset_tag"}},
    };
    return k;
}

std::string to_lower(std::string_view s) {
    std::string out(s);
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

std::string read_file(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw std::runtime_error("cannot read file: " + path.string());
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

void write_file(const std::filesystem::path& path, std::string_view contents) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        throw std::runtime_error("cannot write file: " + path.string());
    }
    out << contents;
}

std::string unquote(std::string val) {
    auto first = val.find_first_not_of(" \t");
    auto last = val.find_last_not_of(" \t");
    if (first == std::string::npos) {
        return {};
    }
    val = val.substr(first, last - first + 1);
    if (!val.empty() && (val.front() == '"' || val.front() == '\'')) {
        const char q = val.front();
        if (val.size() >= 2 && val.back() == q) {
            return val.substr(1, val.size() - 2);
        }
        return val;
    }
    auto comment = val.find(" #");
    if (comment != std::string::npos) {
        val = val.substr(0, comment);
        last = val.find_last_not_of(" \t");
        if (last == std::string::npos) {
            return {};
        }
        val = val.substr(0, last + 1);
    }
    return val;
}

bool is_nullish(std::string_view val) {
    if (val.empty() || val == "~") {
        return true;
    }
    const auto lower = to_lower(val);
    return lower == "null";
}

// Prefer a more specific alias when several keys map to the same canonical field.
int alias_rank(std::string_view canonical, std::string_view normalized_key) {
    if (normalized_key == canonical) {
        return 0;
    }
    if (canonical == "product" && normalized_key == "model") {
        return 1;
    }
    return 2;
}

const std::regex kUuidRe(
    R"(\b[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}\b)",
    std::regex::icase);
const std::regex kMacRe(R"(\b(?:[0-9a-f]{2}[:-]){5}[0-9a-f]{2}\b)",
                        std::regex::icase);

std::string env_var_name(std::string_view canonical) {
    std::string upper = to_lower(canonical);
    std::transform(upper.begin(), upper.end(), upper.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return "CLAUDE_BOX_HOST_" + upper;
}

std::optional<std::string> dmi_name_for(std::string_view canonical) {
    if (canonical == "manufacturer") return std::string{"sys_vendor"};
    if (canonical == "product") return std::string{"product_name"};
    if (canonical == "serial_number") return std::string{"product_serial"};
    if (canonical == "product_uuid") return std::string{"product_uuid"};
    if (canonical == "board_uuid") return std::string{"board_uuid"};
    if (canonical == "chassis_uuid") return std::string{"chassis_uuid"};
    if (canonical == "asset_tag") return std::string{"chassis_asset_tag"};
    return std::nullopt;
}

const std::vector<std::string> kManagedDmiNames = {
    "sys_vendor", "product_name", "product_serial", "product_uuid", "board_uuid",
    "chassis_uuid", "board_serial", "board_asset_tag", "chassis_serial",
    "chassis_asset_tag", "product_sku"};

std::vector<std::filesystem::path> advertised_files(const std::filesystem::path& prefix) {
    std::vector<std::filesystem::path> paths;
    const std::vector<std::filesystem::path> rels = {
        "env/host-model.env",
        "env/session.env",
        "env/spoof.env",
        "presentation",
        "var/host-identity.allowed.yaml",
        "var/host-identity.presented.yaml",
    };
    for (const auto& rel : rels) {
        const auto p = prefix / rel;
        std::error_code ec;
        if (std::filesystem::is_regular_file(p, ec)) {
            paths.push_back(p);
        } else if (std::filesystem::is_directory(p, ec)) {
            for (const auto& entry : std::filesystem::recursive_directory_iterator(p, ec)) {
                if (entry.is_regular_file()) {
                    paths.push_back(entry.path());
                }
            }
        }
    }
    return paths;
}

}  // namespace

std::string normalize_key(std::string_view key) {
    std::string out;
    out.reserve(key.size());
    bool prev_us = false;
    for (unsigned char c : key) {
        if (std::isalnum(c)) {
            out.push_back(static_cast<char>(std::tolower(c)));
            prev_us = false;
        } else if (!out.empty() && !prev_us) {
            out.push_back('_');
            prev_us = true;
        }
    }
    while (!out.empty() && out.back() == '_') {
        out.pop_back();
    }
    return out;
}

std::string_view field_mode_name(FieldMode mode) {
    switch (mode) {
        case FieldMode::Expose:
            return "expose";
        case FieldMode::Hide:
            return "hide";
        case FieldMode::Override:
            return "override";
    }
    return "hide";
}

std::optional<FieldMode> field_mode_from_name(std::string_view name) {
    const auto lower = to_lower(name);
    if (lower == "expose") return FieldMode::Expose;
    if (lower == "hide") return FieldMode::Hide;
    if (lower == "override") return FieldMode::Override;
    return std::nullopt;
}

std::optional<std::string> canonical_field(std::string_view key) {
    const auto n = normalize_key(key);
    for (const auto& spec : specs()) {
        for (const char* alias : spec.aliases) {
            if (normalize_key(alias) == n) {
                return std::string{spec.id};
            }
        }
    }
    return std::nullopt;
}

FieldKind classify_key(std::string_view key) {
    if (const auto id = canonical_field(key)) {
        return kind_of_canonical(*id);
    }
    const auto n = normalize_key(key);
    std::string token;
    std::set<std::string> tokens;
    for (char c : n) {
        if (c == '_') {
            if (!token.empty()) {
                tokens.insert(token);
                token.clear();
            }
        } else {
            token.push_back(c);
        }
    }
    if (!token.empty()) {
        tokens.insert(token);
    }
    if (tokens.count("serial") || tokens.count("uuid") || tokens.count("guid") ||
        tokens.count("mac")) {
        return FieldKind::Identifier;
    }
    return FieldKind::Unknown;
}

std::vector<std::string> canonical_field_ids() {
    std::vector<std::string> ids;
    for (const auto& spec : specs()) {
        ids.emplace_back(spec.id);
    }
    return ids;
}

FieldKind kind_of_canonical(std::string_view canonical) {
    for (const auto& spec : specs()) {
        if (spec.id == canonical) {
            return spec.kind;
        }
    }
    return classify_key(canonical);
}

std::optional<std::string> looks_like_identifier(std::string_view value) {
    std::string s(value);
    if (std::regex_search(s, kUuidRe)) {
        return std::string{"uuid-or-guid"};
    }
    if (std::regex_search(s, kMacRe)) {
        return std::string{"mac-address"};
    }
    return std::nullopt;
}

Policy Policy::defaults() {
    Policy p;
    for (const auto& spec : specs()) {
        FieldPolicy fp;
        fp.mode = (spec.kind == FieldKind::Model) ? FieldMode::Expose : FieldMode::Hide;
        p.fields.emplace(spec.id, std::move(fp));
    }
    return p;
}

FieldPolicy Policy::policy_for(std::string_view canonical) const {
    const std::string id(canonical);
    if (const auto it = fields.find(id); it != fields.end()) {
        return it->second;
    }
    const auto kind = kind_of_canonical(canonical);
    FieldPolicy fp;
    fp.mode = (kind == FieldKind::Model) ? FieldMode::Expose : FieldMode::Hide;
    return fp;
}

Policy Policy::parse_yaml(std::string_view text) {
    Policy p = Policy::defaults();
    std::string current;
    std::istringstream in{std::string(text)};
    std::string line;
    enum class Where { Root, Fields, FieldBody };
    Where where = Where::Root;
    while (std::getline(in, line)) {
        // Keep quoted hashes; for this schema comments start at unquoted '#'.
        std::string raw = line;
        auto first = raw.find_first_not_of(" \t");
        if (first == std::string::npos) {
            continue;
        }
        if (raw[first] == '#') {
            continue;
        }
        const int indent = static_cast<int>(first);
        std::string body = raw.substr(first);
        if (auto cmt = body.find(" #"); cmt != std::string::npos &&
            body.find('"') == std::string::npos && body.find('\'') == std::string::npos) {
            body = body.substr(0, cmt);
            auto last = body.find_last_not_of(" \t");
            if (last != std::string::npos) {
                body = body.substr(0, last + 1);
            }
        }
        if (indent == 0 && (body == "fields:" || body.rfind("fields:", 0) == 0)) {
            where = Where::Fields;
            continue;
        }
        if (where == Where::Root) {
            continue;
        }
        if (indent == 2 && !body.empty() && body.back() == ':') {
            current = normalize_key(body.substr(0, body.size() - 1));
            if (current.empty()) {
                continue;
            }
            if (!p.fields.count(current)) {
                p.fields[current] = FieldPolicy{};
            }
            where = Where::FieldBody;
            continue;
        }
        if (where == Where::FieldBody && indent >= 4 && !current.empty()) {
            auto colon = body.find(':');
            if (colon == std::string::npos) {
                continue;
            }
            auto key = normalize_key(body.substr(0, colon));
            auto val = unquote(body.substr(colon + 1));
            if (key == "mode") {
                auto mode = field_mode_from_name(val);
                if (!mode) {
                    throw std::runtime_error("unknown field mode: " + val);
                }
                p.fields[current].mode = *mode;
            } else if (key == "value") {
                p.fields[current].override_value = val;
            }
        }
    }
    return p;
}

Policy Policy::load_file(const std::filesystem::path& path) {
    return parse_yaml(read_file(path));
}

Inventory parse_flat_yaml(std::string_view text) {
    Inventory out;
    static const std::regex key_re(R"(^([A-Za-z0-9_.]+)\s*:\s*(.*?)\s*$)");
    std::istringstream in{std::string(text)};
    std::string line;
    while (std::getline(in, line)) {
        auto first = line.find_first_not_of(" \t");
        if (first == std::string::npos || line[first] == '#') {
            continue;
        }
        std::smatch m;
        if (!std::regex_match(line, m, key_re)) {
            continue;
        }
        const std::string raw_key = m[1].str();
        const std::string raw_val = m[2].str();
        if (raw_val.empty() || raw_val == "|" || raw_val == ">" || raw_val == "[]") {
            continue;
        }
        const auto val = unquote(raw_val);
        if (is_nullish(val)) {
            continue;
        }
        out[raw_key] = val;
        out[normalize_key(raw_key)] = val;
    }
    return out;
}

Inventory load_inventory_file(const std::filesystem::path& path) {
    return parse_flat_yaml(read_file(path));
}

std::optional<std::string> inventory_value(const Inventory& inventory,
                                           std::string_view canonical) {
    std::optional<std::string> best;
    int best_rank = 100;
    for (const auto& [key, val] : inventory) {
        const auto id = canonical_field(key);
        if (!id || *id != canonical) {
            // Allow direct lookup of unknown policy keys.
            if (normalize_key(key) == normalize_key(canonical) && !val.empty()) {
                if (!best) {
                    best = val;
                }
            }
            continue;
        }
        if (val.empty()) {
            continue;
        }
        const int rank = alias_rank(*id, normalize_key(key));
        if (!best || rank < best_rank) {
            best = val;
            best_rank = rank;
        }
    }
    return best;
}

std::vector<std::string> hidden_identifier_values(const Inventory& inventory,
                                                  const Policy& policy) {
    std::vector<std::string> vals;
    std::set<std::string> seen;
    auto consider = [&](const std::string& val) {
        if (val.empty() || seen.count(val)) {
            return;
        }
        seen.insert(val);
        vals.push_back(val);
    };
    for (const auto& [key, val] : inventory) {
        const auto id = canonical_field(key);
        const std::string field = id ? *id : normalize_key(key);
        const auto kind = id ? kind_of_canonical(*id) : classify_key(key);
        if (kind != FieldKind::Identifier && kind != FieldKind::Unknown) {
            // Model values are hidden only when that model field itself is hidden
            // or overridden (handled below). Skip expose of model here.
        }
        const auto pol = policy.policy_for(field);
        if (pol.mode == FieldMode::Hide) {
            consider(val);
        } else if (pol.mode == FieldMode::Override) {
            // Original value must not leak once replaced.
            if (val != pol.override_value) {
                consider(val);
            }
        }
    }
    return vals;
}

ApplyResult apply_policy(const Inventory& inventory, const Policy& policy) {
    ApplyResult result;
    Presentation pres;

    std::set<std::string> ids;
    for (const auto& spec : specs()) {
        ids.insert(spec.id);
    }
    for (const auto& [id, _] : policy.fields) {
        ids.insert(id);
    }

    for (const auto& id : ids) {
        const auto pol = policy.policy_for(id);
        if (pol.mode == FieldMode::Hide) {
            continue;
        }
        std::string value;
        if (pol.mode == FieldMode::Override) {
            value = pol.override_value;
        } else {
            auto found = inventory_value(inventory, id);
            if (!found || found->empty()) {
                continue;
            }
            value = *found;
        }
        if (kind_of_canonical(id) == FieldKind::Model) {
            if (auto kind = looks_like_identifier(value)) {
                result.error = "host identity field '" + id + "' looks like a " + *kind +
                               ", refusing to publish as a model field";
                result.ok = false;
                return result;
            }
        }
        if (value.empty() && pol.mode != FieldMode::Override) {
            continue;
        }
        pres.fields[id] = value;
    }

    result.ok = true;
    result.presentation = std::move(pres);
    return result;
}

std::string yaml_quote(std::string_view s) {
    if (s.empty()) {
        return "\"\"";
    }
    const std::string specials = ":#{}[]&*!|>'\"%@`";
    if (s.find_first_of(specials) != std::string_view::npos) {
        std::string out = "\"";
        for (char c : s) {
            if (c == '\\' || c == '"') {
                out.push_back('\\');
            }
            out.push_back(c);
        }
        out.push_back('"');
        return out;
    }
    return std::string(s);
}

std::string shell_quote(std::string_view s) {
    std::string out = "'";
    for (char c : s) {
        if (c == '\'') {
            out += "'\"'\"'";
        } else {
            out.push_back(c);
        }
    }
    out.push_back('\'');
    return out;
}

void write_presentation(const std::filesystem::path& prefix,
                        const Presentation& presentation) {
    const auto dmi = prefix / "presentation" / "dmi" / "id";
    std::filesystem::create_directories(dmi);
    std::filesystem::create_directories(prefix / "env");
    std::filesystem::create_directories(prefix / "var");

    std::set<std::string> wanted_dmi;
    for (const auto& [id, val] : presentation.fields) {
        if (auto name = dmi_name_for(id)) {
            write_file(dmi / *name, val + (val.empty() ? "" : "\n"));
            wanted_dmi.insert(*name);
        }
    }
    for (const auto& name : kManagedDmiNames) {
        if (!wanted_dmi.count(name)) {
            std::error_code ec;
            std::filesystem::remove(dmi / name, ec);
        }
    }

    std::ostringstream env;
    env << "# Generated by claudebox identity apply — only policy-approved fields.\n";
    auto write_env = [&](const char* canonical) {
        auto it = presentation.fields.find(canonical);
        if (it == presentation.fields.end()) {
            return;
        }
        env << "export " << env_var_name(canonical) << "=" << shell_quote(it->second)
            << "\n";
    };
    // Stable, known order first; then any extra fields.
    for (const auto& id : canonical_field_ids()) {
        write_env(id.c_str());
    }
    for (const auto& [id, val] : presentation.fields) {
        bool known = false;
        for (const auto& k : canonical_field_ids()) {
            if (k == id) {
                known = true;
                break;
            }
        }
        if (!known) {
            env << "export " << env_var_name(id) << "=" << shell_quote(val) << "\n";
        }
    }
    write_file(prefix / "env" / "host-model.env", env.str());

    std::ostringstream presented;
    presented << "# Presentation copy — only policy-approved fields. Not the raw inventory.\n";
    for (const auto& [id, val] : presentation.fields) {
        presented << id << ": " << yaml_quote(val) << "\n";
    }
    write_file(prefix / "var" / "host-identity.presented.yaml", presented.str());

    std::ostringstream allowed;
    allowed << "# Backward-compatible model fields from the presentation layer.\n";
    if (presentation.fields.count("manufacturer")) {
        allowed << "manufacturer: " << yaml_quote(presentation.fields.at("manufacturer"))
                << "\n";
    }
    if (presentation.fields.count("product")) {
        allowed << "product: " << yaml_quote(presentation.fields.at("product")) << "\n";
    }
    write_file(prefix / "var" / "host-identity.allowed.yaml", allowed.str());

    std::ostringstream manifest;
    manifest << "layer=presentation\n";
    manifest << "fields=";
    bool first = true;
    for (const auto& [id, _] : presentation.fields) {
        if (!first) {
            manifest << ",";
        }
        first = false;
        manifest << id;
    }
    manifest << "\n";
    for (const auto& [id, val] : presentation.fields) {
        manifest << id << "=" << val << "\n";
    }
    manifest << "live_sysfs=not rewritten (kernel DMI stays as-is; this tree is the "
                "contract)\n";
    write_file(prefix / "presentation" / "MANIFEST.txt", manifest.str());
}

std::vector<VerifyIssue> verify(const Inventory& inventory, const Policy& policy,
                                const Presentation& presentation) {
    std::vector<VerifyIssue> issues;

    for (const auto& [id, val] : presentation.fields) {
        const auto pol = policy.policy_for(id);
        if (pol.mode == FieldMode::Hide) {
            issues.push_back({"presented hidden field: " + id});
            continue;
        }
        if (pol.mode == FieldMode::Override) {
            if (val != pol.override_value) {
                issues.push_back({"override mismatch for " + id + ": got '" + val +
                                  "', expected '" + pol.override_value + "'"});
            }
        } else if (pol.mode == FieldMode::Expose) {
            auto expected = inventory_value(inventory, id);
            if (!expected) {
                issues.push_back({"presented " + id + " but inventory has no value"});
            } else if (val != *expected) {
                issues.push_back({"expose mismatch for " + id + ": got '" + val +
                                  "', expected '" + *expected + "'"});
            }
        }
        if (kind_of_canonical(id) == FieldKind::Model) {
            if (auto kind = looks_like_identifier(val)) {
                issues.push_back(
                    {"model field " + id + " looks like a " + *kind});
            }
        }
    }

    std::set<std::string> ids;
    for (const auto& spec : specs()) {
        ids.insert(spec.id);
    }
    for (const auto& [id, _] : policy.fields) {
        ids.insert(id);
    }
    for (const auto& id : ids) {
        const auto pol = policy.policy_for(id);
        if (pol.mode == FieldMode::Hide) {
            continue;
        }
        std::string expected;
        if (pol.mode == FieldMode::Override) {
            expected = pol.override_value;
        } else {
            auto found = inventory_value(inventory, id);
            if (!found || found->empty()) {
                continue;
            }
            expected = *found;
            if (kind_of_canonical(id) == FieldKind::Model && looks_like_identifier(expected)) {
                continue;  // apply would have refused; do not demand it here
            }
        }
        auto it = presentation.fields.find(id);
        if (it == presentation.fields.end()) {
            issues.push_back({"missing presented field: " + id});
        }
    }

    const auto hidden = hidden_identifier_values(inventory, policy);
    std::ostringstream dump;
    for (const auto& [id, val] : presentation.fields) {
        dump << id << "=" << val << "\n";
    }
    const std::string text = dump.str();
    for (const auto& val : hidden) {
        if (!val.empty() && text.find(val) != std::string::npos) {
            issues.push_back({"presentation leaked hidden value: " + val});
        }
    }
    return issues;
}

std::vector<VerifyIssue> verify_prefix(const Inventory& inventory, const Policy& policy,
                                       const std::filesystem::path& prefix) {
    std::vector<VerifyIssue> issues;

    Presentation loaded;
    const auto presented = prefix / "var" / "host-identity.presented.yaml";
    std::error_code ec;
    if (std::filesystem::is_regular_file(presented, ec)) {
        auto parsed = parse_flat_yaml(read_file(presented));
        for (const auto& [key, val] : parsed) {
            if (auto id = canonical_field(key)) {
                loaded.fields[*id] = val;
            } else {
                loaded.fields[normalize_key(key)] = val;
            }
        }
    } else {
        issues.push_back({"missing presentation file: " + presented.string()});
    }

    auto memory_issues = verify(inventory, policy, loaded);
    issues.insert(issues.end(), memory_issues.begin(), memory_issues.end());

    const auto dmi = prefix / "presentation" / "dmi" / "id";
    if (std::filesystem::is_directory(dmi, ec)) {
        for (const auto& entry : std::filesystem::directory_iterator(dmi, ec)) {
            if (!entry.is_regular_file()) {
                continue;
            }
            const auto name = entry.path().filename().string();
            bool allowed = false;
            for (const auto& [id, _] : loaded.fields) {
                auto dname = dmi_name_for(id);
                if (dname && *dname == name) {
                    allowed = true;
                    break;
                }
            }
            if (!allowed) {
                // Known identifier DMI names must not exist unless that field is presented.
                if (name != "sys_vendor" && name != "product_name") {
                    issues.push_back({"forbidden DMI file present: " + entry.path().string()});
                }
            }
        }
    }

    const auto hidden = hidden_identifier_values(inventory, policy);
    for (const auto& path : advertised_files(prefix)) {
        std::string text;
        try {
            text = read_file(path);
        } catch (const std::exception&) {
            continue;
        }
        for (const auto& val : hidden) {
            if (!val.empty() && text.find(val) != std::string::npos) {
                issues.push_back({path.string() + ": leaked identifier value '" + val + "'"});
            }
        }
    }
    return issues;
}

}  // namespace claudebox::identity
