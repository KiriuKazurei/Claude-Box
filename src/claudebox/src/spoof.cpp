#include "claudebox/spoof.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <regex>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace claudebox::spoof {
namespace {

namespace fs = std::filesystem;

std::string trim(std::string_view s) {
    size_t b = 0;
    while (b < s.size() && std::isspace(static_cast<unsigned char>(s[b]))) {
        ++b;
    }
    size_t e = s.size();
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) {
        --e;
    }
    return std::string(s.substr(b, e - b));
}

bool has_path_traversal(std::string_view s) {
    return s.find("..") != std::string_view::npos ||
           s.find('\\') != std::string_view::npos ||
           (!s.empty() && s.front() == '/');
}

bool looks_like_iana(std::string_view tz) {
    if (tz.empty() || tz.size() > 64) {
        return false;
    }
    if (has_path_traversal(tz)) {
        return false;
    }
    // UTC, GMT, or Area/Location (slashes allowed, no spaces).
    for (char c : tz) {
        const auto u = static_cast<unsigned char>(c);
        if (std::isalnum(u) || c == '_' || c == '-' || c == '+' || c == '/') {
            continue;
        }
        return false;
    }
    return true;
}

bool looks_like_locale(std::string_view loc) {
    if (loc.empty() || loc.size() > 64) {
        return false;
    }
    for (char c : loc) {
        const auto u = static_cast<unsigned char>(c);
        if (std::isalnum(u) || c == '_' || c == '-' || c == '.' || c == '@') {
            continue;
        }
        return false;
    }
    return true;
}

std::string read_file(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw std::runtime_error("cannot read " + path.string());
    }
    std::ostringstream out;
    out << in.rdbuf();
    return out.str();
}

void write_file(const fs::path& path, std::string_view text) {
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        throw std::runtime_error("cannot write " + path.string());
    }
    out << text;
    if (!out) {
        throw std::runtime_error("cannot write " + path.string());
    }
}

std::string shell_quote(std::string_view value) {
    std::string escaped;
    escaped.reserve(value.size());
    for (char c : value) {
        if (c == '\\' || c == '"' || c == '$' || c == '`') {
            escaped.push_back('\\');
        }
        escaped.push_back(c);
    }
    return escaped;
}

std::string export_line(std::string_view name, std::string_view value) {
    std::string line = "export ";
    line.append(name);
    line.push_back('=');
    line.push_back('"');
    line.append(shell_quote(value));
    line.push_back('"');
    return line;
}

void upsert_export(std::string& text, std::string_view name, std::string_view value) {
    const std::string line = export_line(name, value);
    const std::regex re("^export " + std::string(name) + "=.*$",
                        std::regex::ECMAScript);
    std::string out;
    std::istringstream in(text);
    std::string existing;
    bool replaced = false;
    while (std::getline(in, existing)) {
        if (!existing.empty() && existing.back() == '\r') {
            existing.pop_back();
        }
        if (std::regex_match(existing, re)) {
            if (!replaced) {
                out.append(line);
                out.push_back('\n');
                replaced = true;
            }
            continue;
        }
        out.append(existing);
        out.push_back('\n');
    }
    if (!replaced) {
        if (!out.empty() && out.back() != '\n') {
            out.push_back('\n');
        }
        out.append(line);
        out.push_back('\n');
    }
    text = std::move(out);
}

std::optional<std::string> read_export(std::string_view text, std::string_view name) {
    const std::string prefix = "export " + std::string(name) + "=";
    std::istringstream in{std::string(text)};
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.rfind(prefix, 0) != 0) {
            continue;
        }
        std::string value = trim(line.substr(prefix.size()));
        if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
            value = value.substr(1, value.size() - 2);
        }
        return value;
    }
    return std::nullopt;
}

fs::path zoneinfo_root() {
    return fs::path("/usr/share/zoneinfo");
}

std::string spoof_env_body(const SpoofValues& values) {
    std::ostringstream out;
    out << export_line("TZ", values.timezone) << "\n";
    out << export_line("LANG", values.locale) << "\n";
    out << export_line("LC_ALL", values.locale) << "\n";
    out << export_line("LANGUAGE", values.language) << "\n";
    return out.str();
}

std::string which_node() {
    if (const char* env = std::getenv("CLAUDE_BOX_NODE")) {
        if (*env && fs::exists(env)) {
            return env;
        }
    }
    for (const char* candidate : {"/usr/local/bin/node", "/usr/bin/node"}) {
        if (fs::exists(candidate)) {
            return candidate;
        }
    }
    if (FILE* pipe = popen("command -v node 2>/dev/null", "r")) {
        char buf[512];
        std::string out;
        while (fgets(buf, sizeof(buf), pipe) != nullptr) {
            out.append(buf);
        }
        const int rc = pclose(pipe);
        out = trim(out);
        if (rc == 0 && !out.empty() && fs::exists(out)) {
            return out;
        }
    }
    return {};
}

}  // namespace

SpoofValues from_config(const claudebox::config::IdentitySpoof& identity) {
    SpoofValues v;
    v.timezone = identity.timezone;
    v.locale = identity.locale;
    v.language = identity.language;
    v.hostname = identity.hostname;
    v.electron_lang = identity.electron_lang;
    return v;
}

std::filesystem::path spoof_env_path(const std::filesystem::path& prefix) {
    return prefix / "env" / "spoof.env";
}

std::filesystem::path session_env_path(const std::filesystem::path& prefix) {
    return prefix / "env" / "session.env";
}

std::filesystem::path prefix_localtime_path(const std::filesystem::path& prefix) {
    return prefix / "etc" / "localtime";
}

std::filesystem::path prefix_timezone_path(const std::filesystem::path& prefix) {
    return prefix / "etc" / "timezone";
}

std::string locale_language_tag(std::string_view locale) {
    std::string s = trim(locale);
    const auto cut = s.find_first_of(".@");
    if (cut != std::string::npos) {
        s.resize(cut);
    }
    for (char& c : s) {
        if (c == '_') {
            c = '-';
        }
    }
    return s;
}

std::filesystem::path zoneinfo_path(std::string_view timezone) {
    return zoneinfo_root() / std::string(timezone);
}

bool timezone_allowed(std::string_view timezone, std::string* error) {
    const std::string tz = trim(timezone);
    if (!looks_like_iana(tz)) {
        if (error) {
            *error = "invalid IANA timezone: " + tz;
        }
        return false;
    }
    const auto root = zoneinfo_root();
    if (!fs::exists(root)) {
        // The box usually has tzdata; if the tree is missing, still accept a
        // well-formed name so tests can run, but record no zone file.
        return true;
    }
    const auto path = zoneinfo_path(tz);
    std::error_code ec;
    const auto canon = fs::weakly_canonical(path, ec);
    const auto root_canon = fs::weakly_canonical(root, ec);
    if (ec || canon.string().rfind(root_canon.string(), 0) != 0) {
        if (error) {
            *error = "timezone path escapes zoneinfo: " + tz;
        }
        return false;
    }
    if (!fs::exists(path)) {
        if (error) {
            *error = "zoneinfo missing for " + tz;
        }
        return false;
    }
    return true;
}

SpoofPlan plan(const SpoofValues& values, const std::filesystem::path& prefix) {
    SpoofPlan p;
    p.values = values;
    p.prefix = prefix;
    p.spoof_env = spoof_env_path(prefix);
    p.session_env = session_env_path(prefix);
    p.etc_dir = prefix / "etc";
    p.localtime = prefix_localtime_path(prefix);
    p.timezone_file = prefix_timezone_path(prefix);
    p.writes_host_etc = false;

    if (prefix.empty()) {
        p.errors.emplace_back("spoof prefix is required");
    }
    if (trim(values.timezone).empty()) {
        p.errors.emplace_back("timezone is required (box spoof value, not the host timezone)");
    } else {
        std::string tz_err;
        if (!timezone_allowed(values.timezone, &tz_err)) {
            p.errors.push_back(tz_err);
        } else if (fs::exists(zoneinfo_root())) {
            p.zoneinfo = zoneinfo_path(trim(values.timezone));
        }
    }
    if (trim(values.locale).empty() || !looks_like_locale(values.locale)) {
        p.errors.emplace_back("invalid box locale: " + values.locale);
    }
    if (trim(values.language).empty() || !looks_like_locale(values.language)) {
        p.errors.emplace_back("invalid box language: " + values.language);
    }
    p.ok = p.errors.empty();
    return p;
}

ApplyResult apply(const SpoofValues& values, const std::filesystem::path& prefix) {
    ApplyResult result;
    result.plan = plan(values, prefix);
    if (!result.plan.ok) {
        result.error = result.plan.errors.empty() ? "spoof plan rejected" : result.plan.errors.front();
        return result;
    }

    try {
        const auto& p = result.plan;
        fs::create_directories(p.spoof_env.parent_path());
        fs::create_directories(p.etc_dir);

        write_file(p.spoof_env, spoof_env_body(values));

        std::string session;
        if (fs::exists(p.session_env)) {
            session = read_file(p.session_env);
        } else {
            session = "# Generated by claudebox::spoof — box spoof values only.\n";
            session += export_line("CLAUDE_BOX_PREFIX", prefix.string()) + "\n";
        }
        upsert_export(session, "TZ", values.timezone);
        upsert_export(session, "LANG", values.locale);
        upsert_export(session, "LC_ALL", values.locale);
        upsert_export(session, "LANGUAGE", values.language);
        if (!values.electron_lang.empty()) {
            upsert_export(session, "CLAUDE_BOX_ELECTRON_LANG", values.electron_lang);
        }
        if (!values.hostname.empty()) {
            upsert_export(session, "CLAUDE_BOX_HOSTNAME", values.hostname);
        }
        write_file(p.session_env, session);

        write_file(p.timezone_file, trim(values.timezone) + "\n");

        if (!p.zoneinfo.empty() && fs::exists(p.zoneinfo)) {
            std::error_code ec;
            if (fs::exists(p.localtime) || fs::is_symlink(p.localtime)) {
                fs::remove(p.localtime, ec);
            }
            fs::create_symlink(p.zoneinfo, p.localtime, ec);
            if (ec) {
                fs::copy_file(p.zoneinfo, p.localtime, fs::copy_options::overwrite_existing);
            }
        }

        result.ok = true;
        return result;
    } catch (const std::exception& ex) {
        result.error = ex.what();
        return result;
    }
}

ApplyResult apply_config(const claudebox::config::IdentitySpoof& identity,
                         const std::filesystem::path& prefix) {
    return apply(from_config(identity), prefix);
}

ProbeResult probe_intl(const SpoofValues& values) {
    ProbeResult probe;
    const std::string node = which_node();
    if (node.empty()) {
        probe.warnings.emplace_back(
            "node not installed — Intl probe skipped (install nodejs for Electron-parity check)");
        return probe;
    }

    // Run node with a clean locale/timezone environment taken from the box
    // spoof values, not the host.
    std::ostringstream cmd;
    cmd << "env -i PATH=" << shell_quote("/usr/local/bin:/usr/bin:/bin")
        << " TZ=\"" << shell_quote(values.timezone) << "\""
        << " LANG=\"" << shell_quote(values.locale) << "\""
        << " LC_ALL=\"" << shell_quote(values.locale) << "\""
        << " LANGUAGE=\"" << shell_quote(values.language) << "\""
        << " HOME=/tmp "
        << node
        << " -e 'const o=Intl.DateTimeFormat().resolvedOptions();"
           "console.log(\"Intl timeZone=\"+o.timeZone+\" locale=\"+o.locale)'";

    std::string output;
    std::string errors;
    FILE* pipe = popen((cmd.str() + " 2>&1").c_str(), "r");
    if (!pipe) {
        probe.errors.emplace_back("failed to start Intl probe");
        return probe;
    }
    char buf[1024];
    while (fgets(buf, sizeof(buf), pipe) != nullptr) {
        output.append(buf);
    }
    const int rc = pclose(pipe);
    probe.ran = true;
    probe.source = "intl-node";

    // setlocale warnings are noise when the generated locale is missing.
    std::istringstream lines(output);
    std::string line;
    std::string intl_line;
    while (std::getline(lines, line)) {
        if (line.find("Intl timeZone=") != std::string::npos) {
            intl_line = line;
        } else if (line.find("setlocale") != std::string::npos ||
                   line.find("warning") != std::string::npos ||
                   line.find("Warning") != std::string::npos) {
            probe.warnings.push_back(trim(line));
        } else if (!trim(line).empty()) {
            errors.append(trim(line));
            errors.push_back('\n');
        }
    }

    const std::regex tz_re("Intl timeZone=([^[:space:]]+)");
    const std::regex loc_re("locale=([^[:space:]]+)");
    std::smatch m;
    if (std::regex_search(intl_line, m, tz_re)) {
        probe.timezone = m[1].str();
    }
    if (std::regex_search(intl_line, m, loc_re)) {
        probe.locale = m[1].str();
    }

    const std::string want_tz = trim(values.timezone);
    const std::string want_loc = locale_language_tag(values.locale);
    if (rc != 0 && probe.timezone.empty()) {
        probe.errors.emplace_back("Intl probe failed: " + trim(output));
        return probe;
    }
    if (probe.timezone != want_tz) {
        probe.errors.emplace_back("Intl timeZone=" + probe.timezone + " != " + want_tz);
    }
    if (!want_loc.empty() && !probe.locale.empty()) {
        // Accept exact BCP-47 tag, or a prefix (en-US matches en).
        const std::string got = probe.locale;
        const bool loc_ok = got == want_loc ||
                            (want_loc.size() >= got.size() && want_loc.compare(0, got.size(), got) == 0) ||
                            (got.size() >= want_loc.size() && got.compare(0, want_loc.size(), want_loc) == 0);
        if (!loc_ok) {
            probe.errors.emplace_back("Intl locale=" + got + " != " + want_loc);
        }
    }
    probe.ok = probe.errors.empty() && !probe.timezone.empty();
    (void)errors;
    return probe;
}

VerifyResult verify(const SpoofValues& values, const std::filesystem::path& prefix) {
    VerifyResult result;
    result.plan = plan(values, prefix);
    if (!result.plan.ok) {
        result.issues = result.plan.errors;
        return result;
    }

    const auto& p = result.plan;
    auto require_export = [&](const fs::path& path, std::string_view name,
                              std::string_view expected) {
        if (!fs::exists(path)) {
            result.issues.push_back("missing " + path.string());
            return;
        }
        const auto text = read_file(path);
        const auto got = read_export(text, name);
        if (!got) {
            result.issues.push_back(path.filename().string() + " missing export " +
                                    std::string(name));
            return;
        }
        if (trim(*got) != trim(std::string(expected))) {
            result.issues.push_back(path.filename().string() + " " + std::string(name) + "=" +
                                    *got + " != " + std::string(expected));
        }
    };

    require_export(p.spoof_env, "TZ", values.timezone);
    require_export(p.spoof_env, "LANG", values.locale);
    require_export(p.spoof_env, "LC_ALL", values.locale);
    require_export(p.spoof_env, "LANGUAGE", values.language);
    require_export(p.session_env, "TZ", values.timezone);
    require_export(p.session_env, "LANG", values.locale);
    require_export(p.session_env, "LC_ALL", values.locale);
    require_export(p.session_env, "LANGUAGE", values.language);

    if (!fs::exists(p.timezone_file)) {
        result.issues.push_back("missing " + p.timezone_file.string());
    } else {
        const auto written = trim(read_file(p.timezone_file));
        if (written != trim(values.timezone)) {
            result.issues.push_back("prefix timezone file is " + written + " != " +
                                    values.timezone);
        }
    }

    if (!p.zoneinfo.empty() && fs::exists(p.zoneinfo)) {
        if (!fs::exists(p.localtime) && !fs::is_symlink(p.localtime)) {
            result.issues.push_back("missing prefix localtime " + p.localtime.string());
        } else if (fs::is_symlink(p.localtime)) {
            std::error_code ec;
            const auto resolved = fs::canonical(p.localtime, ec);
            std::error_code ec2;
            const auto expected = fs::canonical(p.zoneinfo, ec2);
            if (!ec && !ec2 && resolved != expected) {
                result.issues.push_back("prefix localtime resolves to " + resolved.string() +
                                        " != " + expected.string());
            }
        }
    }

    // Never treat the host /etc/localtime as the applied spoof target.
    result.probe = probe_intl(values);
    for (const auto& err : result.probe.errors) {
        result.issues.push_back(err);
    }
    if (result.probe.ran && result.probe.ok) {
        // The host on this box is Asia/Tokyo. A successful probe must match
        // the configured fake value, not the host zone.
        if (result.probe.timezone != trim(values.timezone)) {
            result.issues.push_back("Intl probe did not see the configured box timezone");
        }
    } else if (!result.probe.ran) {
        result.probe.source = "artifacts";
        // Artifacts already checked. Mark probe skipped, verification can
        // still succeed on files alone when node is absent.
    }

    result.ok = result.issues.empty();
    return result;
}

std::string format_status(const SpoofPlan& plan, const VerifyResult* verified) {
    std::ostringstream out;
    out << "这是盒子内的伪装值，不是宿主真实时区或语言。\n";
    out << "保存时只写入盒子前缀（env/spoof.env、env/session.env、etc/localtime），"
           "不会改宿主 /etc/localtime、系统时区或显示语言，"
           "也没有「把宿主真实时区 / 语言交给 Claude」的选项。\n";
    if (!plan.ok) {
        out << "计划未通过：\n";
        for (const auto& err : plan.errors) {
            out << "  - " << err << "\n";
        }
        return out.str();
    }
    out << "时区（IANA）：" << plan.values.timezone << "\n";
    out << "区域设置：" << plan.values.locale << "\n";
    out << "语言：" << plan.values.language << "\n";
    if (!plan.values.electron_lang.empty()) {
        out << "Electron 语言：" << plan.values.electron_lang << "\n";
    }
    if (!plan.values.hostname.empty()) {
        out << "盒子主机名：" << plan.values.hostname << "\n";
    }
    out << "写入：\n";
    out << "  " << plan.spoof_env.string() << "\n";
    out << "  " << plan.session_env.string() << "\n";
    out << "  " << plan.timezone_file.string() << "\n";
    out << "  " << plan.localtime.string();
    if (!plan.zoneinfo.empty()) {
        out << " → " << plan.zoneinfo.string();
    }
    out << "\n";
    out << "不会写入宿主 /etc/localtime。\n";
    if (verified) {
        if (verified->probe.ran) {
            out << "Intl 探针（" << verified->probe.source << "）：timeZone="
                << verified->probe.timezone << " locale=" << verified->probe.locale;
            out << (verified->probe.ok ? "（与配置一致）\n" : "（与配置不一致）\n");
        } else {
            out << "Intl 探针未运行";
            if (!verified->probe.warnings.empty()) {
                out << "：" << verified->probe.warnings.front();
            }
            out << "\n";
        }
        if (!verified->ok) {
            out << "校验未通过：\n";
            for (const auto& issue : verified->issues) {
                out << "  - " << issue << "\n";
            }
        } else {
            out << "校验通过：前缀中的伪装值与配置一致。\n";
        }
    }
    return out.str();
}

}  // namespace claudebox::spoof
