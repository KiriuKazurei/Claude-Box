#include "claudebox/companion.hpp"
#include "claudebox/gateway.hpp"
#include "claudebox/spoof.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <system_error>

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;

namespace claudebox::companion {
namespace {

namespace fs = std::filesystem;

using claudebox::config::BoxConfig;
using claudebox::gateway::validate;
using claudebox::spoof::from_config;
using claudebox::spoof::locale_language_tag;
using claudebox::spoof::session_env_path;

std::string trim(std::string_view in) {
    std::size_t b = 0;
    while (b < in.size() && std::isspace(static_cast<unsigned char>(in[b]))) {
        ++b;
    }
    std::size_t e = in.size();
    while (e > b && std::isspace(static_cast<unsigned char>(in[e - 1]))) {
        --e;
    }
    return std::string(in.substr(b, e - b));
}

std::string to_lower(std::string s) {
    for (char& c : s) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return s;
}

std::string slashify(std::string s) {
    for (char& c : s) {
        if (c == '\\') {
            c = '/';
        }
    }
    return s;
}

std::string read_file(const fs::path& path) {
    std::ifstream in(path);
    if (!in) {
        return {};
    }
    return std::string((std::istreambuf_iterator<char>(in)),
                       std::istreambuf_iterator<char>());
}

void write_file(const fs::path& path, std::string_view body, bool executable = false) {
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        throw std::runtime_error("failed to write " + path.string());
    }
    out << body;
    out.close();
    if (executable) {
        fs::permissions(path,
                        fs::perms::owner_read | fs::perms::owner_write | fs::perms::owner_exec |
                            fs::perms::group_read | fs::perms::group_exec |
                            fs::perms::others_read | fs::perms::others_exec,
                        fs::perm_options::replace);
    }
}

bool path_is_under(const fs::path& path, const fs::path& root) {
    if (root.empty() || path.empty()) {
        return false;
    }
    std::error_code ec;
    const fs::path a = fs::weakly_canonical(path, ec);
    const fs::path canon_path = ec ? path.lexically_normal() : a;
    ec.clear();
    const fs::path b = fs::weakly_canonical(root, ec);
    const fs::path canon_root = ec ? root.lexically_normal() : b;
    const std::string ps = slashify(to_lower(canon_path.string()));
    std::string rs = slashify(to_lower(canon_root.string()));
    if (rs.empty()) {
        return false;
    }
    if (ps == rs) {
        return true;
    }
    if (rs.back() != '/') {
        rs.push_back('/');
    }
    return ps.size() > rs.size() && ps.compare(0, rs.size(), rs) == 0;
}

bool basename_is(const fs::path& path, std::initializer_list<const char*> names) {
    const std::string base = to_lower(path.filename().string());
    for (const char* name : names) {
        if (base == name) {
            return true;
        }
    }
    return false;
}

const char* kHostBrowserNames[] = {
    "xdg-open",
    "x-www-browser",
    "www-browser",
    "gnome-www-browser",
    "gio",
    "gvfs-open",
    "exo-open",
    "open",
    "google-chrome",
    "google-chrome-stable",
    "google-chrome-beta",
    "google-chrome-unstable",
    "chromium",
    "chromium-browser",
    "msedge",
    "microsoft-edge",
    "microsoft-edge-stable",
    "microsoft-edge-beta",
    "firefox",
    "firefox-bin",
    "firefox-esr",
    "brave-browser",
    "opera",
    "safari",
};

bool basename_is_host_browser(const fs::path& path) {
    const std::string base = to_lower(path.filename().string());
    for (const char* name : kHostBrowserNames) {
        if (base == name) {
            return true;
        }
    }
    return false;
}

bool is_system_or_host_install_dir(const fs::path& path) {
    const std::string s = slashify(to_lower(path.string()));
    const char* prefixes[] = {
        "/usr/bin/",
        "/usr/local/bin/",
        "/bin/",
        "/sbin/",
        "/usr/sbin/",
        "/snap/",
        "/usr/lib/",
        "/mnt/c/program files/",
        "/mnt/c/program files (x86)/",
        "c:/program files/",
        "c:/program files (x86)/",
        "/mnt/c/users/",
        "c:/users/",
    };
    for (const char* p : prefixes) {
        if (s.rfind(p, 0) == 0) {
            return true;
        }
    }
    // Exact system bin files: /usr/bin/xdg-open
    const char* exact_dirs[] = {"/usr/bin", "/usr/local/bin", "/bin", "/sbin", "/usr/sbin"};
    const std::string parent = slashify(to_lower(path.parent_path().string()));
    for (const char* d : exact_dirs) {
        if (parent == d) {
            return true;
        }
    }
    return s.find("/appdata/") != std::string::npos;
}

std::string getenv_str(const char* name) {
    const char* v = std::getenv(name);
    return (v && *v) ? std::string(v) : std::string{};
}

fs::path opener_source_script() {
    const auto from_env = getenv_str("CLAUDE_BOX_OPEN_OAUTH");
    if (!from_env.empty() && fs::exists(from_env)) {
        return from_env;
    }
#ifdef CLAUDEBOX_OPEN_OAUTH_SCRIPT
    fs::path from_cmake(CLAUDEBOX_OPEN_OAUTH_SCRIPT);
    if (!from_cmake.empty() && fs::exists(from_cmake)) {
        return from_cmake;
    }
#endif
    return {};
}

std::string shell_single_quote(std::string_view in) {
    std::string out = "'";
    for (char c : in) {
        if (c == '\'') {
            out += "'\\''";
        } else {
            out += c;
        }
    }
    out += "'";
    return out;
}

std::string intercept_wrapper_body(const fs::path& opener) {
    std::ostringstream out;
    out << "#!/usr/bin/env bash\n"
        << "# Claude Box intercept: never fall back to the host default browser.\n"
        << "set -euo pipefail\n"
        << "OPENER=" << shell_single_quote(opener.string()) << "\n"
        << "if [[ ! -x \"$OPENER\" ]]; then\n"
        << "  echo \"error: companion opener is missing or not executable: $OPENER\" >&2\n"
        << "  echo \"  Refusing to open the host default browser (xdg-open/chrome/msedge).\" >&2\n"
        << "  exit 3\n"
        << "fi\n"
        << "exec \"$OPENER\" \"$@\"\n";
    return out.str();
}

std::string companion_env_body(const CompanionPlan& plan, const fs::path& prefix) {
    std::ostringstream out;
    out << "# Generated by claudebox::companion — intercept only.\n"
        << "# Never points BROWSER at a host default browser.\n"
        << "export CLAUDE_BOX_PREFIX=" << shell_single_quote(prefix.string()) << "\n"
        << "export CLAUDE_BOX_COMPANION_PROFILE="
        << shell_single_quote(plan.profile_dir.string()) << "\n"
        << "export CLAUDE_BOX_OPEN_OAUTH=" << shell_single_quote(plan.open_oauth.string())
        << "\n"
        << "export BROWSER=" << shell_single_quote(plan.xdg_open.string()) << "\n";
    if (plan.engine_ready) {
        out << "export CLAUDE_BOX_COMPANION_BROWSER="
            << shell_single_quote(plan.engine.string()) << "\n";
    }
    out << "case \":${PATH}:\" in\n"
        << "  *\":" << plan.intercept_dir.string() << ":\"*) ;;\n"
        << "  *) export PATH=" << shell_single_quote(plan.intercept_dir.string())
        << ":\"${PATH}\" ;;\n"
        << "esac\n";
    return out.str();
}

void upsert_source_line(std::string& text, const fs::path& companion_env) {
    const std::string marker = "# claude-box-companion-intercept";
    const std::string block = marker + "\n. " + shell_single_quote(companion_env.string()) + "\n";
    const auto pos = text.find(marker);
    if (pos == std::string::npos) {
        if (!text.empty() && text.back() != '\n') {
            text.push_back('\n');
        }
        text += block;
        return;
    }
    auto end = text.find('\n', pos);
    if (end != std::string::npos) {
        auto next = text.find('\n', end + 1);
        if (next == std::string::npos) {
            text.replace(pos, text.size() - pos, block);
            if (text.back() != '\n') {
                text.push_back('\n');
            }
            return;
        }
        // Replace marker line + following source line.
        text.replace(pos, next + 1 - pos, block);
        return;
    }
    text.replace(pos, text.size() - pos, block);
}

std::string client_host(const BoxConfig& config) {
    std::string host = trim(config.gateway.host);
    if (host.empty() || host == "auto" || host == "0.0.0.0" || host == "*" || host == "::" ||
        host == "[::]") {
        return "127.0.0.1";
    }
    return host;
}

void add_error(CompanionPlan& plan, std::string message) {
    plan.errors.push_back(std::move(message));
    plan.ok = false;
}

void build_argv_and_env(CompanionPlan& plan) {
    plan.argv.clear();
    plan.env.clear();
    if (!plan.engine.empty()) {
        plan.argv.push_back(plan.engine.string());
        plan.argv.push_back("--user-data-dir=" + plan.profile_dir.string());
        plan.argv.push_back("--no-first-run");
        plan.argv.push_back("--no-default-browser-check");
        if (!plan.http_proxy.empty()) {
            plan.argv.push_back("--proxy-server=" + plan.http_proxy);
        }
        const std::string lang = !plan.electron_lang.empty()
                                     ? plan.electron_lang
                                     : locale_language_tag(plan.locale);
        if (!lang.empty()) {
            plan.argv.push_back("--lang=" + lang);
            plan.argv.push_back("--accept-lang=" + lang);
        }
        if (!plan.url.empty()) {
            plan.argv.push_back(plan.url);
        }
    }

    auto put = [&](const char* key, const std::string& value) {
        if (!value.empty()) {
            plan.env.emplace_back(key, value);
        }
    };
    put("TZ", plan.timezone);
    put("LANG", plan.locale);
    put("LC_ALL", plan.locale);
    put("LANGUAGE", plan.language);
    put("HTTP_PROXY", plan.http_proxy);
    put("HTTPS_PROXY", plan.https_proxy);
    put("ALL_PROXY", plan.all_proxy);
    put("http_proxy", plan.http_proxy);
    put("https_proxy", plan.https_proxy);
    put("all_proxy", plan.all_proxy);
    put("NO_PROXY", plan.no_proxy);
    put("no_proxy", plan.no_proxy);
    if (plan.engine_ready) {
        put("CLAUDE_BOX_COMPANION_BROWSER", plan.engine.string());
    }
    put("CLAUDE_BOX_COMPANION_PROFILE", plan.profile_dir.string());
    if (!plan.xdg_open.empty()) {
        put("BROWSER", plan.xdg_open.string());
    }

    std::ostringstream cmd;
    for (std::size_t i = 0; i < plan.argv.size(); ++i) {
        if (i) {
            cmd << ' ';
        }
        cmd << shell_single_quote(plan.argv[i]);
    }
    plan.command_line = cmd.str();
}

int run_engine(const CompanionPlan& plan, std::string* output) {
    int pipefd[2];
    if (pipe(pipefd) != 0) {
        if (output) {
            *output = "failed to create pipe";
        }
        return 127;
    }

    const pid_t pid = fork();
    if (pid < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        if (output) {
            *output = "fork failed";
        }
        return 127;
    }
    if (pid == 0) {
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        dup2(pipefd[1], STDERR_FILENO);
        if (pipefd[1] != STDOUT_FILENO && pipefd[1] != STDERR_FILENO) {
            close(pipefd[1]);
        }
        for (const auto& [key, value] : plan.env) {
            setenv(key.c_str(), value.c_str(), 1);
        }
        std::vector<char*> argv;
        argv.reserve(plan.argv.size() + 1);
        for (const auto& arg : plan.argv) {
            argv.push_back(const_cast<char*>(arg.c_str()));
        }
        argv.push_back(nullptr);
        execv(plan.engine.c_str(), argv.data());
        _exit(127);
    }
    close(pipefd[1]);
    std::string captured;
    char buf[4096];
    ssize_t n = 0;
    while ((n = read(pipefd[0], buf, sizeof(buf))) > 0) {
        captured.append(buf, static_cast<std::size_t>(n));
    }
    close(pipefd[0]);
    int status = 0;
    if (waitpid(pid, &status, 0) < 0) {
        if (output) {
            *output = captured;
        }
        return 127;
    }
    if (output) {
        *output = captured;
    }
    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }
    if (WIFSIGNALED(status)) {
        return 128 + WTERMSIG(status);
    }
    return 1;
}

}  // namespace

std::filesystem::path default_profile_dir(const std::filesystem::path& prefix) {
    return prefix / "var" / "companion-browser" / "profile";
}

std::filesystem::path default_engine_path(const std::filesystem::path& prefix) {
    return prefix / "opt" / "companion-browser" / "engine";
}

std::filesystem::path intercept_bin_dir(const std::filesystem::path& prefix) {
    return prefix / "bin";
}

std::filesystem::path companion_env_path(const std::filesystem::path& prefix) {
    return prefix / "env" / "companion.env";
}

std::string gateway_proxy_url(const claudebox::config::BoxConfig& config) {
    int port = config.gateway.http_port;
    if (port <= 0) {
        port = 3080;
    }
    return "http://" + client_host(config) + ":" + std::to_string(port);
}

bool is_http_url(std::string_view url) {
    const std::string u = to_lower(trim(url));
    return u.rfind("https://", 0) == 0 || u.rfind("http://", 0) == 0;
}

bool is_oauth_url(std::string_view url) {
    if (!is_http_url(url)) {
        return false;
    }
    const std::string u = to_lower(trim(url));
    const bool known_host = u.find("://claude.ai/") != std::string::npos ||
                            u.find("://claude.ai?") != std::string::npos ||
                            u.find("://console.anthropic.com/") != std::string::npos ||
                            u.find("://console.anthropic.com?") != std::string::npos ||
                            u.find("://accounts.claude.ai/") != std::string::npos ||
                            u.find("://auth.anthropic.com/") != std::string::npos ||
                            u.find("anthropic.com/") != std::string::npos;
    const bool oauthish = u.find("oauth") != std::string::npos ||
                          u.find("authorize") != std::string::npos ||
                          u.find("/login") != std::string::npos ||
                          u.find("/callback") != std::string::npos ||
                          u.find("auth") != std::string::npos;
    return known_host || oauthish;
}

bool is_forbidden_profile_path(const std::filesystem::path& path,
                               const std::filesystem::path& prefix) {
    if (path.empty()) {
        return true;
    }
    if (!prefix.empty() && path_is_under(path, prefix)) {
        return false;
    }
    const std::string s = slashify(to_lower(path.string()));
    const char* needles[] = {
        "/google/chrome",
        "/google/chrome-beta",
        "/microsoft/edge",
        "/.config/google-chrome",
        "/.config/chromium",
        "/.config/microsoft-edge",
        "/appdata/local/google/chrome",
        "/appdata/local/microsoft/edge",
        "/appdata/roaming/claude",
        "/appdata/local/claude",
        "/.config/claude",
        "/.config/claude-desktop",
    };
    for (const char* n : needles) {
        if (s.find(n) != std::string::npos) {
            return true;
        }
    }
    return false;
}

bool is_host_default_browser(const std::filesystem::path& path,
                             const std::filesystem::path& prefix) {
    if (path.empty()) {
        return false;
    }
    // A dedicated engine under the box prefix is never treated as a host browser,
    // even if the basename happens to be "chrome".
    if (!prefix.empty() && path_is_under(path, prefix / "opt" / "companion-browser")) {
        return false;
    }
    if (!prefix.empty() && path_is_under(path, prefix) &&
        !basename_is(path, {"xdg-open", "www-browser", "x-www-browser", "open-oauth-url"})) {
        // Mock engines and user-copied binaries under the prefix are fine,
        // except the intercept wrappers themselves (would recurse / leak).
        if (path_is_under(path, intercept_bin_dir(prefix)) && basename_is_host_browser(path)) {
            return true;
        }
        return false;
    }
    if (!basename_is_host_browser(path)) {
        // An explicit non-browser basename is allowed even outside the prefix
        // (tests drop a mock engine in a temp dir; users may point at a
        // Fingerprint Chromium build somewhere else).
        return is_system_or_host_install_dir(path) && basename_is_host_browser(path);
    }
    return is_system_or_host_install_dir(path) || path_is_under(path, intercept_bin_dir(prefix));
}

std::filesystem::path resolve_engine(const std::filesystem::path& prefix) {
    const auto from_env = getenv_str("CLAUDE_BOX_COMPANION_BROWSER");
    if (!from_env.empty()) {
        return fs::path(from_env);
    }
    if (!prefix.empty()) {
        const auto dedicated = default_engine_path(prefix);
        std::error_code ec;
        if (fs::exists(dedicated, ec)) {
            return dedicated;
        }
    }
    return {};
}

std::filesystem::path resolve_profile(const std::filesystem::path& prefix) {
    const auto from_env = getenv_str("CLAUDE_BOX_COMPANION_PROFILE");
    if (!from_env.empty()) {
        return fs::path(from_env);
    }
    return default_profile_dir(prefix);
}

CompanionPlan plan(const claudebox::config::BoxConfig& config,
                   const std::filesystem::path& prefix,
                   std::string_view url) {
    CompanionPlan p;
    p.feature_enabled = config.features.oauth_open_in_fingerprint_browser;
    p.would_use_host_browser = false;
    p.timezone = config.identity.timezone;
    p.locale = config.identity.locale;
    p.language = config.identity.language;
    p.electron_lang = config.identity.electron_lang;
    p.intercept_dir = intercept_bin_dir(prefix);
    p.xdg_open = p.intercept_dir / "xdg-open";
    p.www_browser = p.intercept_dir / "www-browser";
    p.open_oauth = p.intercept_dir / "open-oauth-url";
    p.companion_env = companion_env_path(prefix);
    p.profile_dir = resolve_profile(prefix);
    p.engine = resolve_engine(prefix);
    p.url = trim(url);

    const auto gw = validate(config, prefix);
    p.proxy_configured = gw.ok && gw.upstream_configured;
    if (p.proxy_configured) {
        p.http_proxy = gateway_proxy_url(config);
        p.https_proxy = p.http_proxy;
        p.all_proxy = p.http_proxy;
    }

    if (p.profile_dir.empty() || is_forbidden_profile_path(p.profile_dir, prefix)) {
        add_error(p,
                  "companion profile must be an isolated directory under the box prefix, "
                  "not a host Chrome/Edge profile and not Claude Electron userData: " +
                      p.profile_dir.string());
    } else if (!prefix.empty() && !path_is_under(p.profile_dir, prefix) &&
               getenv_str("CLAUDE_BOX_COMPANION_PROFILE").empty()) {
        add_error(p, "companion profile is not under the box prefix: " + p.profile_dir.string());
    }

    if (!p.engine.empty()) {
        if (is_host_default_browser(p.engine, prefix)) {
            add_error(p,
                      "refusing to use a host default browser as the companion engine: " +
                          p.engine.string() +
                          " (xdg-open/chrome/msedge are not allowed). "
                          "Set CLAUDE_BOX_COMPANION_BROWSER to an isolated engine, "
                          "or install one at " +
                          default_engine_path(prefix).string());
            p.engine_ready = false;
        } else {
            std::error_code ec;
            const bool exists = fs::exists(p.engine, ec);
            const bool exec = exists && access(p.engine.c_str(), X_OK) == 0;
            p.engine_ready = exec;
            if (!exists) {
                add_error(p, "companion fingerprint browser is not installed: " + p.engine.string());
            } else if (!exec) {
                add_error(p, "companion fingerprint browser is not executable: " + p.engine.string());
            }
        }
    } else {
        p.engine_ready = false;
    }

    if (!p.feature_enabled) {
        add_error(p,
                  "features.oauth_open_in_fingerprint_browser is disabled; "
                  "refusing to open a URL (will not fall back to the host default browser)");
    }
    if (!p.engine_ready) {
        add_error(p,
                  "error: companion fingerprint browser is not installed or not executable. "
                  "Refusing to open the host default browser (xdg-open/chrome/msedge). "
                  "Set CLAUDE_BOX_COMPANION_BROWSER to an isolated Chromium (dedicated profile), "
                  "or place an engine at " +
                      default_engine_path(prefix).string() +
                      ". Policy: docs/companion-fingerprint-browser.md");
    }
    if (!p.proxy_configured) {
        add_error(p,
                  "unique gateway upstream is not configured; refusing to open the companion "
                  "browser because that would bypass the unique gateway");
    }
    if (!p.url.empty() && !is_http_url(p.url)) {
        add_error(p, "refusing non-http(s) URL: " + p.url);
    }

    // Deduplicate errors while preserving order.
    std::vector<std::string> unique;
    for (const auto& e : p.errors) {
        if (std::find(unique.begin(), unique.end(), e) == unique.end()) {
            unique.push_back(e);
        }
    }
    p.errors.swap(unique);

    p.ok = p.errors.empty() && p.feature_enabled && p.engine_ready && p.proxy_configured &&
           (p.url.empty() || is_http_url(p.url));
    // Status-only plans (empty url) can be "ready" when the engine and proxy are in place.
    if (p.url.empty() && p.feature_enabled && p.engine_ready && p.proxy_configured &&
        p.errors.empty()) {
        p.ok = true;
    }
    build_argv_and_env(p);
    return p;
}

InterceptResult install_intercept(const claudebox::config::BoxConfig& config,
                                  const std::filesystem::path& prefix) {
    InterceptResult result;
    result.plan = plan(config, prefix);
    // Intercept install must succeed even when the engine is missing: the
    // wrappers still refuse host-browser fallback at open time.
    try {
        if (prefix.empty()) {
            result.error = "prefix is required to install companion intercept wrappers";
            return result;
        }
        fs::create_directories(result.plan.intercept_dir);
        fs::create_directories(result.plan.profile_dir);
        fs::create_directories(result.plan.companion_env.parent_path());

        fs::path opener_src = opener_source_script();
        // A sourced companion.env sets CLAUDE_BOX_OPEN_OAUTH to the already
        // installed wrapper. Copying that path onto itself throws
        // "File exists" even with overwrite_existing, which would fail a
        // second settings save inside a box session. Prefer the packaged
        // script, and skip the copy when source and destination are the same.
#ifdef CLAUDEBOX_OPEN_OAUTH_SCRIPT
        {
            const fs::path from_cmake(CLAUDEBOX_OPEN_OAUTH_SCRIPT);
            if (!from_cmake.empty() && fs::exists(from_cmake) && !opener_src.empty()) {
                std::error_code ec;
                if (opener_src == result.plan.open_oauth ||
                    (fs::exists(result.plan.open_oauth) &&
                     fs::equivalent(opener_src, result.plan.open_oauth, ec))) {
                    opener_src = from_cmake;
                }
            }
        }
#endif
        if (!opener_src.empty() && fs::exists(opener_src)) {
            std::error_code same_ec;
            const bool same_file =
                fs::exists(result.plan.open_oauth) &&
                (opener_src == result.plan.open_oauth ||
                 fs::equivalent(opener_src, result.plan.open_oauth, same_ec));
            if (!same_file) {
                fs::copy_file(opener_src, result.plan.open_oauth,
                              fs::copy_options::overwrite_existing);
            }
            fs::permissions(result.plan.open_oauth,
                            fs::perms::owner_read | fs::perms::owner_write |
                                fs::perms::owner_exec | fs::perms::group_read |
                                fs::perms::group_exec | fs::perms::others_read |
                                fs::perms::others_exec,
                            fs::perm_options::replace);
        } else if (!fs::exists(result.plan.open_oauth)) {
            result.error =
                "open-oauth-url.sh source is missing; cannot install intercept wrappers";
            return result;
        }

        const auto wrapper = intercept_wrapper_body(result.plan.open_oauth);
        write_file(result.plan.xdg_open, wrapper, true);
        write_file(result.plan.www_browser, wrapper, true);
        write_file(result.plan.companion_env, companion_env_body(result.plan, prefix), false);

        const auto session = session_env_path(prefix);
        std::string text;
        if (fs::exists(session)) {
            text = read_file(session);
        } else {
            text = "# Generated by claudebox::companion\nexport CLAUDE_BOX_PREFIX=" +
                   shell_single_quote(prefix.string()) + "\n";
        }
        upsert_source_line(text, result.plan.companion_env);
        write_file(session, text, false);

        result.ok = true;
        return result;
    } catch (const std::exception& ex) {
        result.error = ex.what();
        return result;
    }
}

OpenResult open_url(const claudebox::config::BoxConfig& config,
                    const std::filesystem::path& prefix,
                    std::string_view url,
                    bool dry_run) {
    OpenResult result;
    result.launched_host_browser = false;
    result.plan = plan(config, prefix, url);
    if (!result.plan.ok) {
        result.error = result.plan.errors.empty()
                           ? "companion plan rejected"
                           : result.plan.errors.front();
        for (std::size_t i = 1; i < result.plan.errors.size(); ++i) {
            result.error.append("\n");
            result.error.append(result.plan.errors[i]);
        }
        result.exit_code = result.plan.engine_ready ? 2 : 3;
        if (!result.plan.feature_enabled) {
            result.exit_code = 4;
        }
        return result;
    }
    if (dry_run) {
        result.ok = true;
        result.exit_code = 0;
        result.output = result.plan.command_line;
        return result;
    }
    if (result.plan.argv.empty()) {
        result.error = "companion plan produced no command";
        result.exit_code = 2;
        return result;
    }
    result.exit_code = run_engine(result.plan, &result.output);
    if (result.exit_code != 0) {
        result.error = "companion engine exited " + std::to_string(result.exit_code);
        if (!result.output.empty()) {
            result.error.append("\n");
            result.error.append(result.output);
        }
        return result;
    }
    result.ok = true;
    return result;
}

std::string format_status(const CompanionPlan& plan) {
    std::ostringstream out;
    out << "伴生指纹浏览器状态（计划 / dry-run）\n";
    out << "OAuth / 授权页必须在伴生浏览器中打开，不会回退到宿主默认浏览器"
        << "（xdg-open / chrome / msedge / www-browser）。\n";
    out << "功能开关 features.oauth_open_in_fingerprint_browser："
        << (plan.feature_enabled ? "开启（默认）" : "关闭") << "\n";
    if (plan.engine_ready) {
        out << "引擎：已就绪\n";
        out << "  " << plan.engine.string() << "\n";
    } else {
        out << "引擎：未就绪\n";
        if (!plan.engine.empty()) {
            out << "  配置路径：" << plan.engine.string() << "\n";
        } else {
            out << "  未设置 CLAUDE_BOX_COMPANION_BROWSER，"
                << "也没有在前缀下找到 opt/companion-browser/engine。\n";
        }
        out << "  引擎未就绪时会拒绝打开，绝不会改用宿主默认浏览器。\n";
        out << "  本模块不会自动下载 Fingerprint Chromium，也不把引擎提交进 git。\n";
    }
    out << "资料目录（独立 user-data-dir）：\n";
    out << "  " << plan.profile_dir.string() << "\n";
    out << "  与宿主 Chrome/Edge 以及 Claude Electron userData 分离。\n";
    if (plan.proxy_configured) {
        out << "出网代理（唯一网关，不是上游 Clash 直连）：" << plan.http_proxy << "\n";
    } else {
        out << "出网代理：未配置唯一网关上游。在上游配好之前不会打开伴生浏览器，以免绕过网关。\n";
    }
    out << "盒子伪装时区 / 语言（不会改宿主）：\n";
    out << "  TZ=" << plan.timezone << "\n";
    out << "  LANG=" << plan.locale << "\n";
    out << "  LANGUAGE=" << plan.language << "\n";
    if (!plan.electron_lang.empty()) {
        out << "  --lang=" << plan.electron_lang << "\n";
    }
    out << "拦截包装：\n";
    out << "  " << plan.xdg_open.string() << "\n";
    out << "  " << plan.www_browser.string() << "\n";
    out << "  BROWSER 将指向盒子前缀内的 xdg-open，不会指向宿主默认浏览器。\n";
    out << "这是云电脑上的计划和状态，不会在这里启动真实的浏览器窗口，也不会改宿主默认浏览器。\n";
    if (!plan.errors.empty()) {
        out << "问题：\n";
        for (const auto& e : plan.errors) {
            out << "  - " << e << "\n";
        }
    }
    return out.str();
}

}  // namespace claudebox::companion
