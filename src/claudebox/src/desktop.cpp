#include "claudebox/desktop.hpp"
#include "claudebox/gateway.hpp"
#include "claudebox/identity.hpp"
#include "claudebox/spoof.hpp"

#include <cctype>
#include <cstdlib>
#include <sstream>
#include <unistd.h>

namespace claudebox::desktop {
namespace {

namespace fs = std::filesystem;

using claudebox::config::BoxConfig;
using claudebox::gateway::BoxProcess;
using claudebox::gateway::is_box_claude_process;
using claudebox::gateway::validate;
using claudebox::spoof::apply_config;
using claudebox::spoof::from_config;

std::string trim(std::string_view in) {
    std::size_t b = 0;
    while (b < in.size() && std::isspace(static_cast<unsigned char>(in[b]))) ++b;
    std::size_t e = in.size();
    while (e > b && std::isspace(static_cast<unsigned char>(in[e - 1]))) --e;
    return std::string(in.substr(b, e - b));
}

std::string getenv_str(const char* name) {
    const char* v = std::getenv(name);
    return (v && *v) ? std::string(v) : std::string{};
}

std::string shell_quote(std::string_view in) {
    std::string out = "'";
    for (char c : in) {
        if (c == '\'') out += "'\\''";
        else out += c;
    }
    out += "'";
    return out;
}

std::string client_proxy(const BoxConfig& config) {
    std::string host = trim(config.gateway.host);
    if (host.empty() || host == "auto" || host == "0.0.0.0" || host == "*" || host == "::") {
        host = "127.0.0.1";
    }
    int port = config.gateway.http_port > 0 ? config.gateway.http_port : 3080;
    return "http://" + host + ":" + std::to_string(port);
}

bool executable(const fs::path& path) {
    std::error_code ec;
    return !path.empty() && fs::exists(path, ec) && access(path.c_str(), X_OK) == 0;
}

}  // namespace

std::filesystem::path session_env_path(const std::filesystem::path& prefix) {
    return claudebox::spoof::session_env_path(prefix);
}

std::filesystem::path host_model_env_path(const std::filesystem::path& prefix) {
    return prefix / "env" / "host-model.env";
}

std::filesystem::path resolve_binary(const claudebox::config::BoxConfig& config,
                                     const std::filesystem::path& prefix) {
    const auto from_env = getenv_str("CLAUDE_BOX_DESKTOP_CMD");
    if (!from_env.empty()) {
        fs::path p(from_env);
        if (executable(p)) return p;
        return p;  // record the configured path even if missing
    }
    std::string cmd = trim(config.desktop.command);
    if (cmd.empty()) cmd = "claude-desktop";
    const fs::path as_path(cmd);
    if (as_path.is_absolute()) {
        return as_path;
    }
    const fs::path under_bin = prefix / "bin" / cmd;
    if (executable(under_bin)) return under_bin;
    const fs::path under_opt = prefix / "opt" / "claude-desktop" / cmd;
    if (executable(under_opt)) return under_opt;
    // Official names only, on typical Linux install prefixes. Do not pick up
    // an unrelated "claude" from PATH during dry-run on this computer.
    for (const char* dir : {"/usr/local/bin", "/usr/bin"}) {
        const fs::path candidate = fs::path(dir) / cmd;
        if (executable(candidate) && cmd == "claude-desktop") {
            return candidate;
        }
    }
    return prefix / "bin" / cmd;
}

DesktopPlan plan(const claudebox::config::BoxConfig& config,
                 const std::filesystem::path& prefix) {
    DesktopPlan p;
    p.prefix = prefix;
    p.command = trim(config.desktop.command);
    if (p.command.empty()) p.command = "claude-desktop";
    p.extra_args = config.desktop.extra_args;
    p.binary = resolve_binary(config, prefix);
    p.binary_found = executable(p.binary);
    p.session_env = claudebox::spoof::session_env_path(prefix);
    p.spoof_env = claudebox::spoof::spoof_env_path(prefix);
    p.host_model_env = host_model_env_path(prefix);
    p.spoof_env_present = fs::exists(p.spoof_env);
    p.host_model_present = fs::exists(p.host_model_env);
    p.timezone = config.identity.timezone;
    p.locale = config.identity.locale;
    p.electron_lang = config.identity.electron_lang;
    p.running = false;

    const auto gw = validate(config, prefix);
    p.proxy_configured = gw.ok && gw.upstream_configured;
    if (p.proxy_configured) {
        p.http_proxy = client_proxy(config);
    } else {
        p.warnings.emplace_back(
            "warn: no HTTP_PROXY/ALL_PROXY — traffic may bypass Claude Box gateway");
        p.errors.emplace_back(
            "unique gateway upstream is not configured; Desktop will not be started "
            "because that would bypass the unique gateway");
    }
    if (!p.binary_found) {
        p.errors.emplace_back(
            "Desktop binary not found (" + p.binary.string() +
            "). Run install-claude-desktop.sh first (cloud: "
            "src/wsl-box/linux/Install-ClaudeDesktop.sh — local deb only). "
            "Not started.");
        p.state = "missing-binary";
    } else if (!p.proxy_configured) {
        p.state = "missing-proxy";
    } else {
        p.state = "planned";
    }

    p.argv.push_back(p.binary.string());
    if (!p.electron_lang.empty()) {
        p.argv.push_back("--lang=" + p.electron_lang);
    }
    for (const auto& extra : p.extra_args) {
        p.argv.push_back(extra);
    }
    std::ostringstream cmd;
    for (std::size_t i = 0; i < p.argv.size(); ++i) {
        if (i) cmd << ' ';
        cmd << shell_quote(p.argv[i]);
    }
    p.command_line = cmd.str();
    p.would_start = p.binary_found && p.proxy_configured;
    p.ok = p.errors.empty();
    // A missing binary is a valid, honest plan: not started.
    if (!p.binary_found && p.proxy_configured) {
        p.ok = true;
    }
    return p;
}

StartResult start(const claudebox::config::BoxConfig& config,
                  const std::filesystem::path& prefix, bool dry_run) {
    StartResult result;
    result.started = false;
    result.plan.running = false;
    try {
        // Apply box spoof values first, same artifacts the start script sources.
        const auto spoofed = apply_config(config.identity, prefix);
        if (!spoofed.ok) {
            result.error = spoofed.error.empty() ? "failed to apply box spoof values"
                                                 : spoofed.error;
            result.plan = plan(config, prefix);
            return result;
        }
        result.plan = plan(config, prefix);
        if (result.plan.proxy_configured && !result.plan.http_proxy.empty()) {
            claudebox::spoof::upsert_proxy_env(result.plan.session_env, result.plan.http_proxy);
        }
        if (!result.plan.binary_found) {
            result.ok = true;  // honest status, not a pretend success
            result.error = result.plan.errors.empty()
                               ? "Desktop binary not found; not started"
                               : result.plan.errors.front();
            result.plan.state = "not-started";
            result.plan.running = false;
            result.plan.would_start = false;
            return result;
        }
        if (!result.plan.proxy_configured) {
            result.ok = false;
            result.error = result.plan.errors.empty()
                               ? "unique gateway proxy is missing; not started"
                               : result.plan.errors.front();
            result.plan.state = "not-started";
            return result;
        }
        if (!dry_run) {
            // Live start is Win11 / WSLg work. This computer only does dry-run.
            result.ok = false;
            result.error =
                "refusing to launch Claude Desktop on this computer; use dry-run here, "
                "and start on Win11 WSL later (W1)";
            result.plan.state = "not-started";
            return result;
        }
        result.ok = true;
        result.started = false;
        result.plan.running = false;
        result.plan.state = "planned";
        return result;
    } catch (const std::exception& ex) {
        result.error = ex.what();
        result.plan.running = false;
        result.started = false;
        return result;
    }
}

StopResult stop(const claudebox::config::BoxConfig& config,
                const std::vector<BoxProcess>& processes, bool dry_run) {
    StopResult result;
    result.dry_run = true;
    result.grace_seconds = config.features.kill_grace_seconds;
    if (result.grace_seconds < 0) result.grace_seconds = 0;
    if (!dry_run) {
        result.error =
            "refusing to signal real processes on this computer; dry-run stop only";
        result.ok = false;
        return result;
    }
    result.processes_after = processes;
    for (const auto& proc : processes) {
        if (is_box_claude_process(proc.name) && proc.alive) {
            result.targets.push_back(proc);
        }
    }
    result.log.push_back("dry-run polite close of box Claude Desktop (not host software)");
    for (const auto& proc : result.targets) {
        result.log.push_back("t=0 polite_close " + proc.name + "#" + std::to_string(proc.pid));
    }
    if (result.grace_seconds > 0 && !result.targets.empty()) {
        result.log.push_back("wait grace " + std::to_string(result.grace_seconds) + "s");
    }
    for (auto& proc : result.processes_after) {
        if (is_box_claude_process(proc.name) && proc.alive) {
            result.log.push_back("t=" + std::to_string(result.grace_seconds) + " force_kill " +
                                 proc.name + "#" + std::to_string(proc.pid));
            proc.alive = false;
        }
    }
    result.ok = true;
    return result;
}

std::string format_status(const DesktopPlan& plan, const StopResult* stopped) {
    std::ostringstream out;
    out << "Linux Claude Desktop 启停状态（dry-run）\n";
    out << "这是计划和状态，不会在这台云电脑上弹出 Desktop 窗口，也不会调用 wsl.exe。\n";
    out << "启动命令：" << plan.command << "\n";
    if (plan.binary_found) {
        out << "二进制：已找到\n  " << plan.binary.string() << "\n";
    } else {
        out << "二进制：未找到\n  " << plan.binary.string() << "\n";
        out << "  未启动。请先用本地 deb 安装（Install-ClaudeDesktop.sh --check / 本地 deb only）。\n";
    }
    if (plan.proxy_configured) {
        out << "唯一网关代理：" << plan.http_proxy << "\n";
    } else {
        out << "唯一网关代理：未配置。缺少代理时不会启动，以免绕过网关。\n";
    }
    out << "盒子伪装：TZ=" << plan.timezone << " LANG=" << plan.locale;
    if (!plan.electron_lang.empty()) out << " --lang=" << plan.electron_lang;
    out << "\n";
    out << "伪装环境：" << (plan.spoof_env_present ? "已有 " : "尚未写入 ")
        << plan.spoof_env.string() << "\n";
    out << "宿主型号展示：" << (plan.host_model_present ? "已有 " : "尚未写入 ")
        << plan.host_model_env.string() << "\n";
    if (!plan.command_line.empty()) {
        out << "计划命令：" << plan.command_line << "\n";
    }
    if (plan.running) {
        out << "会话：运行中（仅在已证明的活会话上显示）\n";
    } else if (plan.would_start) {
        out << "会话：未启动（dry-run 计划可以启动，但本机没有真正拉起窗口）\n";
    } else {
        out << "会话：未启动\n";
    }
    if (stopped && stopped->ok) {
        out << "停止计划（dry-run，礼貌关后 grace " << stopped->grace_seconds
            << " 秒再强杀）：\n";
        for (const auto& line : stopped->log) {
            out << "  " << line << "\n";
        }
    }
    if (!plan.warnings.empty()) {
        out << "警告：\n";
        for (const auto& w : plan.warnings) out << "  - " << w << "\n";
    }
    if (!plan.errors.empty()) {
        out << "说明：\n";
        for (const auto& e : plan.errors) out << "  - " << e << "\n";
    }
    return out.str();
}

}  // namespace claudebox::desktop
