#include "claudebox/gateway.hpp"

#include <algorithm>
#include <cstdio>
#include <cctype>
#include <chrono>
#include <ctime>
#include <fstream>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>

namespace claudebox::gateway {
namespace {

std::string trim_copy(std::string_view s) {
    std::size_t b = 0;
    std::size_t e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) {
        ++b;
    }
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) {
        --e;
    }
    return std::string(s.substr(b, e - b));
}

std::string ascii_lower(std::string_view s) {
    std::string out(s);
    for (char& c : out) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    return out;
}

void strip_dots(std::string& s) {
    while (!s.empty() && s.front() == '.') {
        s.erase(s.begin());
    }
    while (!s.empty() && s.back() == '.') {
        s.pop_back();
    }
}

bool has_supported_scheme(std::string_view lower) {
    return lower.starts_with("http://") || lower.starts_with("https://") ||
           lower.starts_with("socks5://") || lower.starts_with("socks4://") ||
           lower.starts_with("socks://");
}

// Require a scheme and a non-empty host (optional :port, no spaces).
bool looks_like_upstream(std::string_view raw, std::string* error) {
    const std::string trimmed = trim_copy(raw);
    if (trimmed.empty()) {
        return true;
    }
    const std::string lower = ascii_lower(trimmed);
    if (!has_supported_scheme(lower)) {
        if (error) {
            *error = "上游地址必须带 http://、https:// 或 socks5:// 这类协议前缀：" + trimmed;
        }
        return false;
    }
    const auto sep = trimmed.find("://");
    const std::string rest = trimmed.substr(sep + 3);
    if (rest.empty() || rest.find(' ') != std::string::npos) {
        if (error) {
            *error = "上游地址缺少主机名：" + trimmed;
        }
        return false;
    }
    const auto slash = rest.find('/');
    const auto hostport = slash == std::string::npos ? rest : rest.substr(0, slash);
    if (hostport.empty() || hostport == "[" || hostport.front() == ':') {
        if (error) {
            *error = "上游地址缺少主机名：" + trimmed;
        }
        return false;
    }
    return true;
}

std::string expand_env_tokens(std::string path) {
    // Expand %VAR% the same way the PowerShell module does, when the
    // environment variable is present. Leave unresolved tokens as-is.
    std::string out;
    out.reserve(path.size());
    for (std::size_t i = 0; i < path.size();) {
        if (path[i] != '%') {
            out.push_back(path[i]);
            ++i;
            continue;
        }
        const auto end = path.find('%', i + 1);
        if (end == std::string::npos || end == i + 1) {
            out.push_back(path[i]);
            ++i;
            continue;
        }
        const std::string name = path.substr(i + 1, end - i - 1);
        if (const char* val = std::getenv(name.c_str()); val && *val) {
            out.append(val);
            i = end + 1;
        } else {
            // Keep the original token; the caller may fall back to prefix.
            out.append(path.substr(i, end - i + 1));
            i = end + 1;
        }
    }
    return out;
}

bool still_has_unexpanded_env(std::string_view path) {
    const auto first = path.find('%');
    if (first == std::string_view::npos) {
        return false;
    }
    const auto second = path.find('%', first + 1);
    return second != std::string_view::npos && second > first + 1;
}

struct BlockRule {
    std::string host;  // empty = any host (path-only rule)
    std::string path;  // empty = host-only rule
};

BlockRule parse_block_rule(std::string_view raw) {
    BlockRule rule;
    std::string s = ascii_lower(trim_copy(raw));
    if (s.empty()) {
        return rule;
    }
    if (const auto scheme = s.find("://"); scheme != std::string::npos) {
        s = s.substr(scheme + 3);
    }
    if (!s.empty() && s.front() == '/') {
        rule.path = s;
        return rule;
    }
    const auto slash = s.find('/');
    if (slash == std::string::npos) {
        rule.host = s;
        strip_dots(rule.host);
        return rule;
    }
    rule.host = s.substr(0, slash);
    rule.path = s.substr(slash);
    strip_dots(rule.host);
    return rule;
}

std::string normalize_host(std::string_view host) {
    std::string h = ascii_lower(trim_copy(host));
    // Drop a trailing port if it is all digits (sni:443).
    if (const auto colon = h.rfind(':'); colon != std::string::npos && colon > 0) {
        const std::string maybe_port = h.substr(colon + 1);
        if (!maybe_port.empty() &&
            std::all_of(maybe_port.begin(), maybe_port.end(), [](unsigned char c) {
                return std::isdigit(c) != 0;
            })) {
            h.resize(colon);
        }
    }
    strip_dots(h);
    return h;
}

std::string normalize_path(std::string_view path) {
    std::string p = ascii_lower(trim_copy(path));
    if (p.empty()) {
        return {};
    }
    if (const auto hash = p.find('#'); hash != std::string::npos) {
        p.resize(hash);
    }
    if (const auto query = p.find('?'); query != std::string::npos) {
        p.resize(query);
    }
    if (p.empty()) {
        return {};
    }
    if (p.front() != '/') {
        p.insert(p.begin(), '/');
    }
    return p;
}

bool host_suffix_matches(std::string_view host, std::string_view pat) {
    if (pat.empty() || host.empty()) {
        return false;
    }
    if (host == pat) {
        return true;
    }
    const std::string suffix = std::string(".") + std::string(pat);
    return host.size() > suffix.size() && host.ends_with(suffix);
}

bool path_matches(std::string_view request_path, std::string_view rule_path) {
    const std::string want = normalize_path(rule_path);
    const std::string got = normalize_path(request_path);
    if (want.empty() || got.empty()) {
        return false;
    }
    if (got == want) {
        return true;
    }
    if (!got.starts_with(want)) {
        return false;
    }
    // Boundary: "/api/event_logging" must not match "/api/event_logging2".
    const char next = got[want.size()];
    return next == '/' || next == '?' || next == '#';
}

std::string now_utc_iso() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
    gmtime_r(&t, &tm);
    char buf[64];
    if (std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S+00:00", &tm) == 0) {
        return "1970-01-01T00:00:00+00:00";
    }
    return buf;
}

std::string json_escape(std::string_view s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (unsigned char c : s) {
        switch (c) {
            case '\\':
                out += "\\\\";
                break;
            case '"':
                out += "\\\"";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\r':
                out += "\\r";
                break;
            case '\t':
                out += "\\t";
                break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out.push_back(static_cast<char>(c));
                }
                break;
        }
    }
    return out;
}

bool skip_ws(std::string_view s, std::size_t& i) {
    while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) {
        ++i;
    }
    return i < s.size();
}

bool parse_json_string(std::string_view s, std::size_t& i, std::string* out) {
    if (i >= s.size() || s[i] != '"') {
        return false;
    }
    ++i;
    std::string value;
    while (i < s.size()) {
        const char c = s[i];
        if (c == '"') {
            ++i;
            if (out) {
                *out = std::move(value);
            }
            return true;
        }
        if (c == '\\') {
            ++i;
            if (i >= s.size()) {
                return false;
            }
            const char e = s[i++];
            switch (e) {
                case '"':
                case '\\':
                case '/':
                    value.push_back(e);
                    break;
                case 'n':
                    value.push_back('\n');
                    break;
                case 'r':
                    value.push_back('\r');
                    break;
                case 't':
                    value.push_back('\t');
                    break;
                case 'u':
                    if (i + 4 > s.size()) {
                        return false;
                    }
                    i += 4;  // keep the field parseable; tests use ASCII
                    break;
                default:
                    value.push_back(e);
                    break;
            }
            continue;
        }
        value.push_back(c);
        ++i;
    }
    return false;
}

struct JsonValue {
    enum Kind { Null, Bool, Number, String } kind = Null;
    bool b = false;
    std::int64_t n = 0;
    std::string s;
};

bool parse_json_value(std::string_view s, std::size_t& i, JsonValue* out) {
    if (!skip_ws(s, i)) {
        return false;
    }
    JsonValue v;
    if (s[i] == '"') {
        if (!parse_json_string(s, i, &v.s)) {
            return false;
        }
        v.kind = JsonValue::String;
        *out = std::move(v);
        return true;
    }
    if (s.compare(i, 4, "null") == 0) {
        i += 4;
        v.kind = JsonValue::Null;
        *out = std::move(v);
        return true;
    }
    if (s.compare(i, 4, "true") == 0) {
        i += 4;
        v.kind = JsonValue::Bool;
        v.b = true;
        *out = std::move(v);
        return true;
    }
    if (s.compare(i, 5, "false") == 0) {
        i += 5;
        v.kind = JsonValue::Bool;
        v.b = false;
        *out = std::move(v);
        return true;
    }
    if (s[i] == '-' || std::isdigit(static_cast<unsigned char>(s[i]))) {
        const std::size_t start = i;
        if (s[i] == '-') {
            ++i;
        }
        while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i]))) {
            ++i;
        }
        if (i < s.size() && s[i] == '.') {
            ++i;
            while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i]))) {
                ++i;
            }
        }
        try {
            v.n = static_cast<std::int64_t>(std::stoll(std::string(s.substr(start, i - start))));
        } catch (const std::exception&) {
            return false;
        }
        v.kind = JsonValue::Number;
        *out = std::move(v);
        return true;
    }
    return false;
}

int listen_port_of(const claudebox::config::BoxConfig& config) {
    return config.gateway.http_port;
}

std::string listen_host_of(const claudebox::config::BoxConfig& config) {
    const std::string host = trim_copy(config.gateway.host);
    if (host.empty() || ascii_lower(host) == "auto") {
        // Architecture: the gateway may listen on 0.0.0.0 (or a WSL-visible
        // host IP). Dry-run does not resolve a Win11 default-gateway IP.
        return "0.0.0.0";
    }
    return host;
}

std::pair<std::string, std::string> choose_upstream(const claudebox::config::GatewaySpec& gw) {
    const std::string http = trim_copy(gw.upstream_http);
    const std::string socks = trim_copy(gw.upstream_socks5);
    if (!http.empty()) {
        return {"http", http};
    }
    if (!socks.empty()) {
        return {"socks5", socks};
    }
    return {"", ""};
}


std::string yaml_quote(std::string_view s) {
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

std::string shell_quote(std::string_view s) {
    std::string out = "'";
    for (char c : s) {
        if (c == '\'') {
            out += "'\\''";
        } else {
            out.push_back(c);
        }
    }
    out.push_back('\'');
    return out;
}

std::string regex_escape(std::string_view s) {
    static const std::string specials = ".^$*+?()[]{}|\\";
    std::string out;
    for (char c : s) {
        if (specials.find(c) != std::string::npos) {
            out.push_back('\\');
        }
        out.push_back(c);
    }
    return out;
}

// PathRegexp equivalent of C++ path_matches: exact path or next char is / ? #.
// PathPrefix("/api/event_logging") would wrongly match /api/event_logging2.
std::string path_boundary_regexp(std::string_view path) {
    return "^" + regex_escape(path) + R"((?:/|\?|#|$))";
}

std::vector<std::string> gost_host_bypass_matchers(const std::vector<std::string>& blocklist) {
    std::vector<std::string> out;
    std::set<std::string> seen;
    auto add = [&](const std::string& m) {
        if (seen.insert(m).second) {
            out.push_back(m);
        }
    };
    for (const auto& raw : blocklist) {
        const BlockRule rule = parse_block_rule(raw);
        if (rule.host.empty() || !rule.path.empty()) {
            continue;
        }
        add(rule.host);
        if (rule.host.find('*') == std::string::npos && rule.host.front() != '.') {
            add("*." + rule.host);
        }
    }
    return out;
}

std::vector<std::string> gost_path_regexps(const std::vector<std::string>& blocklist) {
    std::vector<std::string> out;
    std::set<std::string> seen;
    for (const auto& raw : blocklist) {
        const BlockRule rule = parse_block_rule(raw);
        if (rule.path.empty()) {
            continue;
        }
        const std::string re = path_boundary_regexp(rule.path);
        if (seen.insert(re).second) {
            out.push_back(re);
        }
    }
    return out;
}

std::string render_gost_relay_yaml(const std::string& listen_host, int listen_port,
                                   const std::string& upstream_url,
                                   const std::vector<std::string>& blocklist) {
    const auto hosts = gost_host_bypass_matchers(blocklist);
    const auto paths = gost_path_regexps(blocklist);
    std::ostringstream yml;
    yml << "# Claude Box unique-gateway plan for go-gost v3 (https://gost.run/).\n"
        << "# Load with: gost -C <this-file>. Dry-run / plan only unless an operator starts it.\n"
        << "# Requires go-gost 3.3.0+ when path rules are present (hopGroup + PathRegexp).\n"
        << "#\n"
        << "# Host-suffix denials use bypass in the default blacklist mode: a matching\n"
        << "# destination is not forwarded (deny), not leaked around the proxy.\n"
        << "# Path rules (event_logging, /api/v2/rum) use hopGroup matchers. PathRegexp\n"
        << "# is used instead of PathPrefix so /api/event_logging2 and /api/v2/rumble\n"
        << "# stay unblocked, matching C++ path_matches.\n"
        << "# Residual: HTTPS CONNECT to api.anthropic.com does not expose the path\n"
        << "# without TLS MITM, which is out of default/MVP. Host-level Sentry/Datadog\n"
        << "# denies still apply to CONNECT/SNI. The main API is not host-denied.\n"
        << "# Docs: https://v3.gost.run/en/concepts/bypass/\n"
        << "#       https://v3.gost.run/en/concepts/hop-and-forwarder/\n\n";

    if (!hosts.empty()) {
        yml << "bypasses:\n"
            << "  - name: bypass-telemetry-hosts\n"
            << "    # default blacklist: match => terminate forwarding\n"
            << "    matchers:\n";
        for (const auto& m : hosts) {
            yml << "      - " << yaml_quote(m) << "\n";
        }
        yml << "  - name: bypass-deny-all\n"
            << "    matchers:\n"
            << "      - \"0.0.0.0/0\"\n"
            << "      - \"::/0\"\n"
            << "      - \"*\"\n\n";
    } else if (!paths.empty()) {
        yml << "bypasses:\n"
            << "  - name: bypass-deny-all\n"
            << "    matchers:\n"
            << "      - \"0.0.0.0/0\"\n"
            << "      - \"::/0\"\n"
            << "      - \"*\"\n\n";
    }

    if (!paths.empty()) {
        yml << "hops:\n"
            << "  - name: hop-deny-telemetry-paths\n"
            << "    # Matching HTTP paths land here; bypass-all terminates forwarding.\n"
            << "    bypass: bypass-deny-all\n"
            << "    nodes:\n"
            << "      - name: deny\n"
            << "        addr: \"127.0.0.1:1\"\n"
            << "        connector:\n"
            << "          type: tcp\n"
            << "        dialer:\n"
            << "          type: tcp\n"
            << "  - name: hop-upstream\n"
            << "    nodes:\n"
            << "      - name: upstream\n"
            << "        addr: " << yaml_quote(upstream_url) << "\n"
            << "        connector:\n"
            << "          type: http\n"
            << "        dialer:\n"
            << "          type: tcp\n\n";
    }

    yml << "services:\n"
        << "  - name: claude-box-in\n"
        << "    addr: " << yaml_quote(listen_host + ":" + std::to_string(listen_port)) << "\n"
        << "    handler:\n"
        << "      type: http\n"
        << "    listener:\n"
        << "      type: tcp\n";
    if (!hosts.empty()) {
        yml << "    bypass: bypass-telemetry-hosts\n";
    }
    if (!paths.empty()) {
        yml << "    forwarder:\n"
            << "      hopGroup:\n"
            << "        hops:\n"
            << "          - hop: hop-deny-telemetry-paths\n"
            << "            matcher:\n"
            << "              rule: '";
        for (std::size_t i = 0; i < paths.size(); ++i) {
            if (i != 0) {
                yml << " || ";
            }
            yml << "PathRegexp(\"" << paths[i] << "\")";
        }
        yml << "'\n"
            << "          - hop: hop-upstream\n";
    } else {
        yml << "    forwarder:\n"
            << "      nodes:\n"
            << "        - name: upstream\n"
            << "          addr: " << yaml_quote(upstream_url) << "\n";
    }
    return yml.str();
}

}  // namespace

std::filesystem::path default_log_path(const std::filesystem::path& prefix) {
    return prefix / "var" / "logs" / "gateway.jsonl";
}

std::filesystem::path resolve_log_path(std::string_view configured,
                                       const std::filesystem::path& prefix) {
    const std::string trimmed = trim_copy(configured);
    if (trimmed.empty()) {
        return default_log_path(prefix);
    }
    const std::string expanded = expand_env_tokens(trimmed);
    if (still_has_unexpanded_env(expanded)) {
        return default_log_path(prefix);
    }
    const std::filesystem::path p(expanded);
    if (p.is_absolute()) {
        return p;
    }
    return prefix / p;
}

bool host_blocked(std::string_view host, const std::vector<std::string>& blocklist) {
    const std::string h = normalize_host(host);
    if (h.empty() || blocklist.empty()) {
        return false;
    }
    for (const auto& raw : blocklist) {
        const BlockRule rule = parse_block_rule(raw);
        if (rule.host.empty() || !rule.path.empty()) {
            continue;
        }
        if (host_suffix_matches(h, rule.host)) {
            return true;
        }
    }
    return false;
}

bool request_blocked(std::string_view host, std::string_view path,
                     const std::vector<std::string>& blocklist) {
    const std::string h = normalize_host(host);
    if (blocklist.empty()) {
        return false;
    }
    const std::string p = normalize_path(path);
    for (const auto& raw : blocklist) {
        const BlockRule rule = parse_block_rule(raw);
        if (rule.path.empty()) {
            if (!h.empty() && host_suffix_matches(h, rule.host)) {
                return true;
            }
            continue;
        }
        const bool host_ok = rule.host.empty() || host_suffix_matches(h, rule.host);
        if (host_ok && path_matches(p, rule.path)) {
            return true;
        }
    }
    return false;
}

Validation validate(const claudebox::config::BoxConfig& config,
                    const std::filesystem::path& prefix) {
    Validation v;
    const auto& gw = config.gateway;
    v.configured_host = trim_copy(gw.host);
    if (v.configured_host.empty()) {
        v.configured_host = "auto";
    }
    v.listen_host = listen_host_of(config);
    v.listen_port = listen_port_of(config);
    v.logging_enabled = gw.logging.enabled;
    v.logging_path = resolve_log_path(gw.logging.path, prefix);

    const std::string http = trim_copy(gw.upstream_http);
    const std::string socks = trim_copy(gw.upstream_socks5);
    v.http_configured = !http.empty();
    v.socks5_configured = !socks.empty();
    v.upstream_configured = v.http_configured || v.socks5_configured;

    if (v.listen_port < 1 || v.listen_port > 65535) {
        v.errors.emplace_back("网关端口必须在 1–65535 之间");
    }

    std::string url_error;
    if (v.http_configured && !looks_like_upstream(http, &url_error)) {
        v.errors.push_back("上游 HTTP " + url_error);
    }
    url_error.clear();
    if (v.socks5_configured && !looks_like_upstream(socks, &url_error)) {
        v.errors.push_back("上游 SOCKS5 " + url_error);
    }
    if (!v.upstream_configured) {
        v.errors.emplace_back(
            "需要至少配置一个上游：gateway.upstream_http 或 gateway.upstream_socks5");
    }

    const auto chosen = choose_upstream(gw);
    v.primary = chosen.first;
    v.upstream_url = chosen.second;
    v.ok = v.errors.empty();
    return v;
}

RelayPlan plan_relay(const claudebox::config::BoxConfig& config,
                     const std::filesystem::path& prefix) {
    const Validation v = validate(config, prefix);
    RelayPlan plan;
    plan.ok = v.ok;
    plan.errors = v.errors;
    plan.configured_host = v.configured_host;
    plan.listen_host = v.listen_host;
    plan.listen_port = v.listen_port;
    plan.listen = "http://" + v.listen_host + ":" + std::to_string(v.listen_port);
    plan.primary = v.primary;
    plan.upstream_http = trim_copy(config.gateway.upstream_http);
    plan.upstream_socks5 = trim_copy(config.gateway.upstream_socks5);
    plan.upstream_url = v.upstream_url;
    plan.forward = v.upstream_url;
    plan.blocklist = config.gateway.blocklist;
    plan.logging_enabled = v.logging_enabled;
    plan.logging_path = v.logging_path;
    plan.status = "plan-only";
    plan.tool = "gost";
    if (!plan.forward.empty()) {
        plan.gost_yaml = render_gost_relay_yaml(plan.listen_host, plan.listen_port,
                                               plan.forward, plan.blocklist);
        if (!prefix.empty()) {
            plan.gost_yaml_path = prefix / "var" / "gateway" / "gost.relay.yml";
            try {
                std::filesystem::create_directories(plan.gost_yaml_path.parent_path());
                std::ofstream out(plan.gost_yaml_path, std::ios::binary | std::ios::trunc);
                if (!out) {
                    plan.errors.emplace_back("无法写入 gost 计划文件：" +
                                             plan.gost_yaml_path.string());
                    plan.ok = false;
                } else {
                    out << plan.gost_yaml;
                    if (!out) {
                        plan.errors.emplace_back("写入 gost 计划文件失败：" +
                                                 plan.gost_yaml_path.string());
                        plan.ok = false;
                    }
                }
            } catch (const std::exception& ex) {
                plan.errors.emplace_back(std::string("写入 gost 计划文件失败：") + ex.what());
                plan.ok = false;
            }
            plan.command_line = "gost -C " + shell_quote(plan.gost_yaml_path.string());
        } else {
            plan.command_line = "gost -C gost.relay.yml";
        }
    }
    return plan;
}

bool parse_log_line(std::string_view line, LogEntry* out) {
    const std::string trimmed = trim_copy(line);
    if (trimmed.empty()) {
        return false;
    }
    if (trimmed.front() != '{' || trimmed.back() != '}') {
        return false;
    }
    std::size_t i = 1;
    LogEntry entry;
    bool any = false;
    while (skip_ws(trimmed, i) && trimmed[i] != '}') {
        std::string key;
        if (!parse_json_string(trimmed, i, &key)) {
            return false;
        }
        if (!skip_ws(trimmed, i) || trimmed[i] != ':') {
            return false;
        }
        ++i;
        JsonValue value;
        if (!parse_json_value(trimmed, i, &value)) {
            return false;
        }
        any = true;
        if (key == "ts" && value.kind == JsonValue::String) {
            entry.ts = value.s;
        } else if (key == "src" && value.kind == JsonValue::String) {
            entry.src = value.s;
        } else if (key == "dst_ip" && value.kind == JsonValue::String) {
            entry.dst_ip = value.s;
        } else if (key == "dst_port" && value.kind == JsonValue::Number) {
            entry.dst_port = static_cast<int>(value.n);
        } else if (key == "sni_or_host" && value.kind == JsonValue::String) {
            entry.sni_or_host = value.s;
        } else if (key == "proto" && value.kind == JsonValue::String) {
            entry.proto = value.s;
        } else if (key == "action" && value.kind == JsonValue::String) {
            entry.action = value.s;
        } else if (key == "upstream_ok") {
            if (value.kind == JsonValue::Bool) {
                entry.upstream_ok = value.b;
            } else if (value.kind == JsonValue::Null) {
                entry.upstream_ok.reset();
            }
        } else if (key == "bytes") {
            if (value.kind == JsonValue::Number) {
                entry.bytes = value.n;
            } else if (value.kind == JsonValue::Null) {
                entry.bytes.reset();
            }
        } else if (key == "note" && value.kind == JsonValue::String) {
            entry.note = value.s;
        }
        if (!skip_ws(trimmed, i)) {
            return false;
        }
        if (trimmed[i] == ',') {
            ++i;
            continue;
        }
        if (trimmed[i] == '}') {
            break;
        }
        return false;
    }
    if (!any) {
        return false;
    }
    if (out) {
        *out = std::move(entry);
    }
    return true;
}

std::string format_log_line(const LogEntry& entry) {
    std::ostringstream out;
    out << '{';
    out << "\"ts\":\"" << json_escape(entry.ts) << "\"";
    out << ",\"src\":\"" << json_escape(entry.src) << "\"";
    out << ",\"dst_ip\":\"" << json_escape(entry.dst_ip) << "\"";
    out << ",\"dst_port\":" << entry.dst_port;
    out << ",\"sni_or_host\":\"" << json_escape(entry.sni_or_host) << "\"";
    out << ",\"proto\":\"" << json_escape(entry.proto.empty() ? "tcp" : entry.proto) << "\"";
    const std::string action = entry.action.empty() ? "proxy" : entry.action;
    out << ",\"action\":\"" << json_escape(action) << "\"";
    out << ",\"upstream_ok\":";
    if (entry.upstream_ok.has_value()) {
        out << (*entry.upstream_ok ? "true" : "false");
    } else {
        out << "null";
    }
    out << ",\"bytes\":";
    if (entry.bytes.has_value()) {
        out << *entry.bytes;
    } else {
        out << "null";
    }
    if (!entry.note.empty()) {
        out << ",\"note\":\"" << json_escape(entry.note) << "\"";
    }
    out << '}';
    return out.str();
}

std::string append_log_entry(const std::filesystem::path& path, const LogEntry& entry) {
    LogEntry copy = entry;
    if (copy.ts.empty()) {
        copy.ts = now_utc_iso();
    }
    if (copy.action.empty()) {
        copy.action = "proxy";
    }
    if (copy.proto.empty()) {
        copy.proto = "tcp";
    }
    if (copy.action != "allow" && copy.action != "deny" && copy.action != "proxy") {
        throw std::runtime_error("gateway log action must be allow, deny, or proxy");
    }
    const auto parent = path.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent);
    }
    const std::string line = format_log_line(copy);
    std::ofstream out(path, std::ios::app);
    if (!out) {
        throw std::runtime_error("failed to append gateway log: " + path.string());
    }
    out << line << '\n';
    if (!out) {
        throw std::runtime_error("failed to append gateway log: " + path.string());
    }
    return line;
}

LogSummary read_log(const std::filesystem::path& path, std::size_t last_n) {
    LogSummary sum;
    sum.path = path;
    sum.file_exists = std::filesystem::exists(path) && std::filesystem::is_regular_file(path);
    if (!sum.file_exists) {
        return sum;
    }
    std::ifstream in(path);
    if (!in) {
        return sum;
    }
    std::vector<LogEntry> all;
    std::string line;
    while (std::getline(in, line)) {
        if (trim_copy(line).empty()) {
            continue;
        }
        LogEntry entry;
        if (!parse_log_line(line, &entry)) {
            ++sum.skipped;
            continue;
        }
        ++sum.total;
        if (entry.action == "deny") {
            ++sum.deny_count;
        } else if (entry.action == "allow") {
            ++sum.allow_count;
        } else if (entry.action == "proxy") {
            ++sum.proxy_count;
        }
        all.push_back(std::move(entry));
    }
    if (last_n == 0 || all.size() <= last_n) {
        sum.recent = std::move(all);
    } else {
        sum.recent.assign(all.end() - static_cast<std::ptrdiff_t>(last_n), all.end());
    }
    return sum;
}

GatewayStatus status(const claudebox::config::BoxConfig& config,
                     const std::filesystem::path& prefix, std::size_t recent_n) {
    GatewayStatus st;
    const RelayPlan plan = plan_relay(config, prefix);
    st.plan = plan;
    st.errors = plan.errors;
    st.upstream_configured = plan.ok && !plan.upstream_url.empty();
    st.configured_host = plan.configured_host;
    st.listen_host = plan.listen_host;
    st.listen_port = plan.listen_port;
    st.planned_listen = plan.listen;
    st.upstream_http = plan.upstream_http;
    st.upstream_socks5 = plan.upstream_socks5;
    st.upstream_url = plan.upstream_url;
    st.logging_enabled = plan.logging_enabled;
    st.logging_path = plan.logging_path;
    st.process_state =
        "dry-run：未启动 gost，也未在本机绑定 Win11 网关进程";
    st.log = read_log(plan.logging_path, recent_n);
    st.log_file_exists = st.log.file_exists;
    return st;
}

std::string format_status(const GatewayStatus& st) {
    std::ostringstream out;
    out << "唯一网关状态（dry-run，不会启动 gost，也不会绑定 Win11 网卡）\n";
    out << "计划监听：" << st.planned_listen;
    if (!st.configured_host.empty()) {
        out << "（配置宿主：" << st.configured_host << "）";
    }
    out << "\n";
    out << "上游 HTTP：" << (st.upstream_http.empty() ? "（未配置）" : st.upstream_http) << "\n";
    out << "上游 SOCKS5：" << (st.upstream_socks5.empty() ? "（未配置）" : st.upstream_socks5)
        << "\n";
    out << "选用上游：" << (st.upstream_url.empty() ? "（无）" : st.upstream_url) << "\n";
    out << "上游：" << (st.upstream_configured ? "已配置" : "未配置") << "\n";
    out << "中继工具：" << st.plan.tool << "\n";
    if (!st.plan.command_line.empty()) {
        out << "中继计划：" << st.plan.command_line << "\n";
    }
    out << "进程：" << st.process_state << "\n";
    out << "计划状态：" << st.plan.status << "\n\n";
    out << "日志：" << (st.logging_enabled ? "启用" : "关闭") << "\n";
    out << "日志路径：" << st.logging_path.string() << "\n";
    out << "日志文件：" << (st.log_file_exists ? "存在" : "尚不存在") << "\n";

    out << "\n拦截名单：\n";
    if (st.plan.blocklist.empty()) {
        out << "  （空）\n";
    } else {
        for (const auto& item : st.plan.blocklist) {
            out << "  • " << item << "\n";
        }
    }

    if (!st.errors.empty()) {
        out << "\n错误（保存会被拒绝）：\n";
        for (const auto& err : st.errors) {
            out << "  • " << err << "\n";
        }
    }

    out << "\n最近活动（" << st.log.recent.size() << " 条，共 " << st.log.total
        << " 条，deny=" << st.log.deny_count << " proxy=" << st.log.proxy_count
        << " allow=" << st.log.allow_count << "）：\n";
    if (!st.log.file_exists) {
        out << "  （还没有日志文件）\n";
    } else if (st.log.recent.empty()) {
        out << "  （日志为空）\n";
    } else {
        for (const auto& e : st.log.recent) {
            out << "  • " << (e.ts.empty() ? "?" : e.ts) << "  " << e.action << "  "
                << (e.sni_or_host.empty() ? e.dst_ip : e.sni_or_host);
            if (e.dst_port > 0) {
                out << " :" << e.dst_port;
            }
            if (!e.note.empty()) {
                out << "  (" << e.note << ")";
            }
            out << "\n";
        }
    }
    return out.str();
}

}  // namespace claudebox::gateway
