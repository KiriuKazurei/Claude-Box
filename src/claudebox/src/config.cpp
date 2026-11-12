#include "claudebox/config.hpp"

#include "claudebox/identity.hpp"

#include <cctype>
#include <fstream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace claudebox::config {
namespace {

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

std::string to_lower(std::string s) {
    for (char& c : s) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return s;
}

// Unquote a YAML scalar, honoring double-quoted escapes so that
// "D:\\Projects\\demo" becomes D:\Projects\demo.
std::string unquote(std::string val) {
    auto first = val.find_first_not_of(" \t");
    if (first == std::string::npos) {
        return {};
    }
    auto last = val.find_last_not_of(" \t");
    val = val.substr(first, last - first + 1);

    if (val.size() >= 2 && ((val.front() == '"' && val.back() == '"') ||
                            (val.front() == '\'' && val.back() == '\''))) {
        const char q = val.front();
        const std::string inner = val.substr(1, val.size() - 2);
        if (q == '\'') {
            return inner;
        }
        std::string out;
        out.reserve(inner.size());
        for (size_t i = 0; i < inner.size(); ++i) {
            if (inner[i] == '\\' && i + 1 < inner.size()) {
                const char n = inner[i + 1];
                if (n == '\\' || n == '"' || n == '/') {
                    out.push_back(n);
                    ++i;
                    continue;
                }
                if (n == 'n') {
                    out.push_back('\n');
                    ++i;
                    continue;
                }
                if (n == 't') {
                    out.push_back('\t');
                    ++i;
                    continue;
                }
            }
            out.push_back(inner[i]);
        }
        return out;
    }

    // Unquoted: strip an unquoted trailing comment.
    std::string bare;
    bool in_single = false, in_double = false;
    for (size_t i = 0; i < val.size(); ++i) {
        const char c = val[i];
        if (c == '"' && !in_single) {
            in_double = !in_double;
        } else if (c == '\'' && !in_double) {
            in_single = !in_single;
        }
        if (!in_single && !in_double && c == '#' &&
            (i == 0 || val[i - 1] == ' ' || val[i - 1] == '\t')) {
            bare = val.substr(0, i);
            auto end = bare.find_last_not_of(" \t");
            if (end == std::string::npos) {
                return {};
            }
            return bare.substr(0, end + 1);
        }
    }
    return val;
}

struct Node {
    enum class Type { Null, Scalar, Map, List };
    Type type = Type::Null;
    std::string scalar;
    std::vector<std::pair<std::string, Node>> map;
    std::vector<Node> list;

    const Node* child(std::string_view key) const {
        if (type != Type::Map) {
            return nullptr;
        }
        for (const auto& [k, v] : map) {
            if (k == key) {
                return &v;
            }
        }
        return nullptr;
    }

    std::string str(std::string_view key, std::string def = {}) const {
        const Node* n = child(key);
        if (!n) {
            return def;
        }
        if (n->type == Type::Scalar) {
            return n->scalar;
        }
        return def;
    }

    bool boolean(std::string_view key, bool def = false) const {
        const Node* n = child(key);
        if (!n || n->type != Type::Scalar) {
            return def;
        }
        const auto lower = to_lower(n->scalar);
        if (lower == "true" || lower == "yes" || lower == "on") {
            return true;
        }
        if (lower == "false" || lower == "no" || lower == "off") {
            return false;
        }
        return def;
    }

    int integer(std::string_view key, int def = 0) const {
        const Node* n = child(key);
        if (!n || n->type != Type::Scalar || n->scalar.empty()) {
            return def;
        }
        try {
            return std::stoi(n->scalar);
        } catch (...) {
            return def;
        }
    }

    std::vector<std::string> string_list(std::string_view key) const {
        const Node* n = child(key);
        if (!n) {
            return {};
        }
        std::vector<std::string> out;
        if (n->type == Type::List) {
            for (const auto& item : n->list) {
                if (item.type == Type::Scalar) {
                    out.push_back(item.scalar);
                }
            }
        } else if (n->type == Type::Scalar && !n->scalar.empty()) {
            out.push_back(n->scalar);
        }
        return out;
    }
};

struct RawLine {
    int indent = 0;
    std::string body;  // left-trimmed, comments already handled at start
};

std::string strip_comment_outside_quotes(const std::string& s) {
    bool in_single = false, in_double = false;
    for (size_t i = 0; i < s.size(); ++i) {
        const char c = s[i];
        if (c == '"' && !in_single) {
            in_double = !in_double;
        } else if (c == '\'' && !in_double) {
            in_single = !in_single;
        } else if (c == '#' && !in_single && !in_double &&
                   (i == 0 || s[i - 1] == ' ' || s[i - 1] == '\t')) {
            return s.substr(0, i);
        }
    }
    return s;
}

std::vector<RawLine> tokenize(std::string_view text) {
    std::vector<RawLine> lines;
    std::istringstream in{std::string(text)};
    std::string raw;
    while (std::getline(in, raw)) {
        if (!raw.empty() && raw.back() == '\r') {
            raw.pop_back();
        }
        auto first = raw.find_first_not_of(" \t");
        if (first == std::string::npos) {
            continue;
        }
        if (raw[first] == '#') {
            continue;
        }
        RawLine line;
        line.indent = static_cast<int>(first);
        line.body = strip_comment_outside_quotes(raw.substr(first));
        auto last = line.body.find_last_not_of(" \t");
        if (last == std::string::npos) {
            continue;
        }
        line.body = line.body.substr(0, last + 1);
        if (line.body.empty()) {
            continue;
        }
        lines.push_back(std::move(line));
    }
    return lines;
}

std::vector<std::string> parse_inline_list(const std::string& inner) {
    std::vector<std::string> out;
    std::string cur;
    bool in_single = false, in_double = false;
    for (size_t i = 0; i < inner.size(); ++i) {
        const char c = inner[i];
        if (c == '"' && !in_single) {
            in_double = !in_double;
            cur.push_back(c);
        } else if (c == '\'' && !in_double) {
            in_single = !in_single;
            cur.push_back(c);
        } else if (c == ',' && !in_single && !in_double) {
            auto v = unquote(cur);
            if (!v.empty()) {
                out.push_back(v);
            }
            cur.clear();
        } else {
            cur.push_back(c);
        }
    }
    auto v = unquote(cur);
    if (!v.empty()) {
        out.push_back(v);
    }
    return out;
}

Node parse_block(const std::vector<RawLine>& lines, size_t& i, int min_indent);

Node make_scalar_or_inline(const std::string& raw) {
    Node n;
    const auto trimmed = unquote(raw);
    if (raw.find_first_not_of(" \t") != std::string::npos) {
        const auto start = raw.find_first_not_of(" \t");
        if (raw[start] == '[') {
            auto end = raw.find_last_of(']');
            if (end != std::string::npos && end > start) {
                n.type = Node::Type::List;
                for (const auto& item : parse_inline_list(raw.substr(start + 1, end - start - 1))) {
                    Node s;
                    s.type = Node::Type::Scalar;
                    s.scalar = item;
                    n.list.push_back(std::move(s));
                }
                return n;
            }
        }
    }
    n.type = Node::Type::Scalar;
    n.scalar = trimmed;
    return n;
}

Node parse_list(const std::vector<RawLine>& lines, size_t& i, int indent) {
    Node list;
    list.type = Node::Type::List;
    while (i < lines.size()) {
        const auto& line = lines[i];
        if (line.indent < indent) {
            break;
        }
        if (line.indent > indent) {
            throw std::runtime_error("unexpected indent in list: " + line.body);
        }
        if (line.body.rfind("-", 0) != 0 ||
            (line.body.size() > 1 && line.body[1] != ' ' && line.body[1] != '\t' &&
             line.body[1] != '\0')) {
            // Allow "-key" isn't typical; require "- " or bare "-"
            if (line.body != "-" && line.body.rfind("- ", 0) != 0 &&
                line.body.rfind("-\t", 0) != 0) {
                break;
            }
        }
        std::string rest;
        if (line.body == "-") {
            rest.clear();
        } else if (line.body.size() >= 2) {
            rest = line.body.substr(2);
            auto f = rest.find_first_not_of(" \t");
            rest = (f == std::string::npos) ? std::string{} : rest.substr(f);
        }
        ++i;

        if (rest.empty()) {
            // Nested block as the list item.
            if (i < lines.size() && lines[i].indent > indent) {
                list.list.push_back(parse_block(lines, i, lines[i].indent));
            } else {
                Node empty;
                empty.type = Node::Type::Scalar;
                list.list.push_back(std::move(empty));
            }
            continue;
        }

        auto colon = std::string::npos;
        bool in_single = false, in_double = false;
        for (size_t k = 0; k < rest.size(); ++k) {
            const char c = rest[k];
            if (c == '"' && !in_single) in_double = !in_double;
            else if (c == '\'' && !in_double) in_single = !in_single;
            else if (c == ':' && !in_single && !in_double) {
                colon = k;
                break;
            }
        }

        if (colon == std::string::npos) {
            list.list.push_back(make_scalar_or_inline(rest));
            continue;
        }

        // List item starts a map: "- key: value"
        Node item;
        item.type = Node::Type::Map;
        const std::string key = unquote(rest.substr(0, colon));
        const std::string after = rest.substr(colon + 1);
        const auto af = after.find_first_not_of(" \t");
        if (af == std::string::npos) {
            if (i < lines.size() && lines[i].indent > indent) {
                item.map.emplace_back(key, parse_block(lines, i, lines[i].indent));
            } else {
                Node empty;
                empty.type = Node::Type::Scalar;
                item.map.emplace_back(key, std::move(empty));
            }
        } else {
            item.map.emplace_back(key, make_scalar_or_inline(after.substr(af)));
        }
        // Continuation keys for this map: indent > list dash indent.
        while (i < lines.size() && lines[i].indent > indent &&
               lines[i].body.rfind("- ", 0) != 0 && lines[i].body != "-") {
            const int child_indent = lines[i].indent;
            // Parse as a one-entry or nested map at this indent and merge.
            Node more = parse_block(lines, i, child_indent);
            if (more.type == Node::Type::Map) {
                for (auto& kv : more.map) {
                    item.map.push_back(std::move(kv));
                }
            }
        }
        list.list.push_back(std::move(item));
    }
    return list;
}

Node parse_map(const std::vector<RawLine>& lines, size_t& i, int indent) {
    Node map;
    map.type = Node::Type::Map;
    while (i < lines.size()) {
        const auto& line = lines[i];
        if (line.indent < indent) {
            break;
        }
        if (line.indent > indent) {
            throw std::runtime_error("unexpected indent in map: " + line.body);
        }
        if (line.body.rfind("- ", 0) == 0 || line.body == "-") {
            break;
        }
        auto colon = std::string::npos;
        bool in_single = false, in_double = false;
        for (size_t k = 0; k < line.body.size(); ++k) {
            const char c = line.body[k];
            if (c == '"' && !in_single) in_double = !in_double;
            else if (c == '\'' && !in_double) in_single = !in_single;
            else if (c == ':' && !in_single && !in_double) {
                colon = k;
                break;
            }
        }
        if (colon == std::string::npos) {
            throw std::runtime_error("expected key: " + line.body);
        }
        const std::string key = unquote(line.body.substr(0, colon));
        const std::string after = line.body.substr(colon + 1);
        const auto af = after.find_first_not_of(" \t");
        ++i;
        if (af == std::string::npos) {
            if (i < lines.size() && lines[i].indent > indent) {
                if (lines[i].body.rfind("- ", 0) == 0 || lines[i].body == "-" ||
                    lines[i].body.rfind("-\t", 0) == 0) {
                    map.map.emplace_back(key, parse_list(lines, i, lines[i].indent));
                } else {
                    map.map.emplace_back(key, parse_map(lines, i, lines[i].indent));
                }
            } else {
                Node empty;
                empty.type = Node::Type::Scalar;
                map.map.emplace_back(key, std::move(empty));
            }
        } else {
            map.map.emplace_back(key, make_scalar_or_inline(after.substr(af)));
        }
    }
    return map;
}

Node parse_block(const std::vector<RawLine>& lines, size_t& i, int min_indent) {
    if (i >= lines.size()) {
        Node n;
        n.type = Node::Type::Map;
        return n;
    }
    if (lines[i].indent < min_indent) {
        Node n;
        n.type = Node::Type::Map;
        return n;
    }
    if (lines[i].body.rfind("- ", 0) == 0 || lines[i].body == "-" ||
        lines[i].body.rfind("-\t", 0) == 0) {
        return parse_list(lines, i, lines[i].indent);
    }
    return parse_map(lines, i, lines[i].indent);
}

const Node* walk(const Node& root, std::initializer_list<const char*> keys) {
    const Node* cur = &root;
    for (const char* key : keys) {
        cur = cur->child(key);
        if (!cur) {
            return nullptr;
        }
    }
    return cur;
}

std::string yq(std::string_view s) { return claudebox::identity::yaml_quote(s); }

void write_bool(std::ostringstream& out, int indent, std::string_view key, bool v) {
    out << std::string(static_cast<size_t>(indent), ' ') << key << ": "
        << (v ? "true" : "false") << "\n";
}

void write_str(std::ostringstream& out, int indent, std::string_view key, std::string_view v) {
    out << std::string(static_cast<size_t>(indent), ' ') << key << ": " << yq(v) << "\n";
}

void write_int(std::ostringstream& out, int indent, std::string_view key, int v) {
    out << std::string(static_cast<size_t>(indent), ' ') << key << ": " << v << "\n";
}

void write_string_list(std::ostringstream& out, int indent, std::string_view key,
                       const std::vector<std::string>& items) {
    const std::string pad(static_cast<size_t>(indent), ' ');
    if (items.empty()) {
        out << pad << key << ": []\n";
        return;
    }
    out << pad << key << ":\n";
    for (const auto& item : items) {
        out << pad << "  - " << yq(item) << "\n";
    }
}

}  // namespace

BoxConfig BoxConfig::defaults() { return BoxConfig{}; }

BoxConfig BoxConfig::parse_yaml(std::string_view text) {
    auto lines = tokenize(text);
    size_t i = 0;
    Node root = parse_block(lines, i, 0);
    BoxConfig cfg = BoxConfig::defaults();

    if (auto v = root.str("apiVersion"); !v.empty()) {
        cfg.api_version = v;
    }
    if (auto v = root.str("kind"); !v.empty()) {
        cfg.kind = v;
    }
    if (const Node* meta = root.child("metadata")) {
        if (auto v = meta->str("name"); !v.empty()) {
            cfg.name = v;
        }
        if (auto v = meta->str("distro_name"); !v.empty()) {
            cfg.distro_name = v;
        }
    }

    const Node* spec = root.child("spec");
    if (!spec) {
        // Allow a flattened document that already starts at spec keys.
        spec = &root;
    }

    if (const Node* base = spec->child("base")) {
        if (base->child("preferred_source")) {
            cfg.base.preferred_source = base->str("preferred_source");
        }
        if (base->child("install_dir")) {
            cfg.base.install_dir = base->str("install_dir");
        }
    }
    if (const Node* gw = spec->child("gateway")) {
        if (gw->child("host")) {
            cfg.gateway.host = gw->str("host");
        }
        if (gw->child("http_port")) {
            cfg.gateway.http_port = gw->integer("http_port", cfg.gateway.http_port);
        }
        if (gw->child("upstream_http")) {
            cfg.gateway.upstream_http = gw->str("upstream_http");
        }
        if (gw->child("upstream_socks5")) {
            cfg.gateway.upstream_socks5 = gw->str("upstream_socks5");
        }
        if (gw->child("blocklist")) {
            cfg.gateway.blocklist = gw->string_list("blocklist");
        }
        if (const Node* log = gw->child("logging")) {
            if (log->child("enabled")) {
                cfg.gateway.logging.enabled = log->boolean("enabled", true);
            }
            if (log->child("path")) {
                cfg.gateway.logging.path = log->str("path");
            }
        }
    }
    if (const Node* dirs = spec->child("visible_dirs")) {
        cfg.visible_dirs.clear();
        if (dirs->type == Node::Type::List) {
            for (const auto& item : dirs->list) {
                VisibleDir d;
                if (item.type == Node::Type::Map) {
                    d.host_path = item.str("host_path");
                    d.guest_path = item.str("guest_path");
                    d.read_only = item.boolean("read_only", false);
                } else if (item.type == Node::Type::Scalar) {
                    d.host_path = item.scalar;
                }
                cfg.visible_dirs.push_back(std::move(d));
            }
        }
    }
    if (const Node* mount = spec->child("mount")) {
        if (mount->child("disable_automount")) {
            cfg.mount.disable_automount = mount->boolean("disable_automount", true);
        }
    }
    if (const Node* id = spec->child("identity")) {
        if (id->child("timezone")) cfg.identity.timezone = id->str("timezone");
        if (id->child("locale")) cfg.identity.locale = id->str("locale");
        if (id->child("language")) cfg.identity.language = id->str("language");
        if (id->child("hostname")) cfg.identity.hostname = id->str("hostname");
        if (id->child("electron_lang")) cfg.identity.electron_lang = id->str("electron_lang");
    }
    if (const Node* hi = spec->child("host_identity")) {
        if (hi->child("expose_real_model")) {
            cfg.host_identity.expose_real_model = hi->boolean("expose_real_model", true);
        }
        if (hi->child("manufacturer")) {
            cfg.host_identity.manufacturer = hi->str("manufacturer");
        }
        if (hi->child("product")) {
            cfg.host_identity.product = hi->str("product");
        }
        if (hi->child("hide_identifiers")) {
            cfg.host_identity.hide_identifiers = hi->boolean("hide_identifiers", true);
        }
        if (hi->child("hidden_identifier_keys")) {
            cfg.host_identity.hidden_identifier_keys = hi->string_list("hidden_identifier_keys");
        }
    }
    if (const Node* desk = spec->child("desktop")) {
        if (desk->child("deb_url")) cfg.desktop.deb_url = desk->str("deb_url");
        if (desk->child("deb_local")) cfg.desktop.deb_local = desk->str("deb_local");
        if (desk->child("command")) cfg.desktop.command = desk->str("command");
        if (desk->child("extra_args")) cfg.desktop.extra_args = desk->string_list("extra_args");
        if (desk->child("asar_patch")) cfg.desktop.asar_patch = desk->boolean("asar_patch", false);
    }
    if (const Node* feat = spec->child("features")) {
        if (feat->child("exclude_host_system_drive")) {
            cfg.features.exclude_host_system_drive =
                feat->boolean("exclude_host_system_drive", true);
        }
        if (feat->child("computer_use")) {
            cfg.features.computer_use = feat->boolean("computer_use", false);
        }
        if (feat->child("kill_on_local_ip_change")) {
            cfg.features.kill_on_local_ip_change = feat->boolean("kill_on_local_ip_change", false);
        }
        if (feat->child("kill_grace_seconds")) {
            cfg.features.kill_grace_seconds = feat->integer("kill_grace_seconds", 10);
        }
        if (feat->child("watch_interval_seconds")) {
            cfg.features.watch_interval_seconds = feat->integer("watch_interval_seconds", 5);
        }
        if (feat->child("oauth_open_in_fingerprint_browser")) {
            cfg.features.oauth_open_in_fingerprint_browser =
                feat->boolean("oauth_open_in_fingerprint_browser", true);
        }
        if (feat->child("identity_vault")) {
            cfg.features.identity_vault = feat->boolean("identity_vault", true);
        }
    }

    (void)walk;
    return cfg;
}

BoxConfig BoxConfig::load_file(const std::filesystem::path& path) {
    return parse_yaml(read_file(path));
}

std::string BoxConfig::to_yaml() const {
    std::ostringstream out;
    out << "# Claude Box · WSL2 轨配置。由设置页写入。勿提交含真实代理口令或真实硬件编码的文件。\n";
    out << "apiVersion: " << yq(api_version) << "\n";
    out << "kind: " << yq(kind) << "\n\n";
    out << "metadata:\n";
    write_str(out, 2, "name", name);
    write_str(out, 2, "distro_name", distro_name);
    out << "\nspec:\n";

    out << "  base:\n";
    write_str(out, 4, "preferred_source", base.preferred_source);
    write_str(out, 4, "install_dir", base.install_dir);

    out << "\n  gateway:\n";
    write_str(out, 4, "host", gateway.host);
    write_int(out, 4, "http_port", gateway.http_port);
    write_str(out, 4, "upstream_http", gateway.upstream_http);
    write_str(out, 4, "upstream_socks5", gateway.upstream_socks5);
    write_string_list(out, 4, "blocklist", gateway.blocklist);
    out << "    logging:\n";
    write_bool(out, 6, "enabled", gateway.logging.enabled);
    write_str(out, 6, "path", gateway.logging.path);

    out << "\n  # 只挂这些宿主目录；禁止写 C:\\ 根或整棵 Users。校验在 P5 起由核心挂载计划执行。\n";
    out << "  visible_dirs:\n";
    if (visible_dirs.empty()) {
        out << "    []\n";
    } else {
        for (const auto& d : visible_dirs) {
            out << "    - host_path: " << yq(d.host_path) << "\n";
            out << "      guest_path: " << yq(d.guest_path) << "\n";
            write_bool(out, 6, "read_only", d.read_only);
        }
    }

    out << "\n  mount:\n";
    write_bool(out, 4, "disable_automount", mount.disable_automount);

    out << "\n  # 盒子内伪装值，不是把宿主真实时区 / 语言交给 Claude。\n";
    out << "  identity:\n";
    write_str(out, 4, "timezone", identity.timezone);
    write_str(out, 4, "locale", identity.locale);
    write_str(out, 4, "language", identity.language);
    write_str(out, 4, "hostname", identity.hostname);
    write_str(out, 4, "electron_lang", identity.electron_lang);

    out << "\n  # hide_identifiers 是过渡约束，必须为 true。按字段授权请用 var/host-identity.policy.yaml。\n";
    out << "  host_identity:\n";
    write_bool(out, 4, "expose_real_model", host_identity.expose_real_model);
    write_str(out, 4, "manufacturer", host_identity.manufacturer);
    write_str(out, 4, "product", host_identity.product);
    write_bool(out, 4, "hide_identifiers", true);  // always persist the transitional constraint
    write_string_list(out, 4, "hidden_identifier_keys", host_identity.hidden_identifier_keys);

    out << "\n  desktop:\n";
    write_str(out, 4, "deb_url", desktop.deb_url);
    write_str(out, 4, "deb_local", desktop.deb_local);
    write_str(out, 4, "command", desktop.command);
    write_string_list(out, 4, "extra_args", desktop.extra_args);
    write_bool(out, 4, "asar_patch", desktop.asar_patch);

    out << "\n  features:\n";
    write_bool(out, 4, "exclude_host_system_drive", features.exclude_host_system_drive);
    write_bool(out, 4, "computer_use", features.computer_use);
    write_bool(out, 4, "kill_on_local_ip_change", features.kill_on_local_ip_change);
    write_int(out, 4, "kill_grace_seconds", features.kill_grace_seconds);
    write_int(out, 4, "watch_interval_seconds", features.watch_interval_seconds);
    write_bool(out, 4, "oauth_open_in_fingerprint_browser",
               features.oauth_open_in_fingerprint_browser);
    write_bool(out, 4, "identity_vault", features.identity_vault);
    return out.str();
}

void BoxConfig::save_file(const std::filesystem::path& path) const {
    if (is_example_config_path(path)) {
        throw std::runtime_error(
            "refusing to overwrite example config: " + path.string() +
            " (copy it to the box prefix first)");
    }
    BoxConfig copy = *this;
    copy.host_identity.hide_identifiers = true;
    write_file(path, copy.to_yaml());
}

std::filesystem::path default_config_path(const std::filesystem::path& prefix) {
    return prefix / "config" / "wsl-box.yaml";
}

bool is_example_config_path(const std::filesystem::path& path) {
    const auto name = path.filename().string();
    return name == "wsl-box.example.yaml";
}

}  // namespace claudebox::config
