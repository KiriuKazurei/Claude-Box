#include "claudebox/gateway_ip.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <string>
#include <string_view>

namespace claudebox::gateway {
namespace {

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

std::string basename_of(std::string_view name) {
    std::string s = trim(name);
    const auto slash = s.find_last_of("/\\");
    if (slash != std::string::npos) {
        s = s.substr(slash + 1);
    }
    // Drop a trailing .exe for Windows-style names, but keep the
    // documented Linux names exact otherwise.
    constexpr std::string_view exe = ".exe";
    if (s.size() > exe.size()) {
        const auto tail = s.substr(s.size() - exe.size());
        if (tail == ".exe" || tail == ".EXE") {
            s.resize(s.size() - exe.size());
        }
    }
    return s;
}

bool ipv4_loopback(std::string_view addr) {
    // 127.0.0.0/8
    if (addr.size() < 4 || addr.rfind("127.", 0) != 0) {
        return false;
    }
    return true;
}

bool ipv6_loopback(std::string_view addr) {
    return addr == "::1" || addr == "0:0:0:0:0:0:0:1" || addr == "::1/128";
}

std::string address_from_entry(std::string_view raw) {
    std::string s = trim(raw);
    // "iface|address/prefix" or "address/prefix" or "address"
    const auto bar = s.find('|');
    if (bar != std::string::npos) {
        s = s.substr(bar + 1);
    }
    const auto slash = s.find('/');
    if (slash != std::string::npos) {
        s = s.substr(0, slash);
    }
    // Drop IPv6 zone id ("fe80::1%eth0")
    const auto zone = s.find('%');
    if (zone != std::string::npos) {
        s = s.substr(0, zone);
    }
    return trim(s);
}

std::string normalize_entry(std::string_view raw) {
    return trim(raw);
}

}  // namespace

std::string IpFingerprint::text() const {
    std::string out;
    for (size_t i = 0; i < entries.size(); ++i) {
        if (i) {
            out.push_back('\n');
        }
        out.append(entries[i]);
    }
    return out;
}

bool is_loopback_address(std::string_view address) {
    const auto addr = address_from_entry(address);
    if (addr.empty()) {
        return true;
    }
    return ipv4_loopback(addr) || ipv6_loopback(addr);
}

IpFingerprint make_fingerprint(const std::vector<std::string>& raw_entries) {
    IpFingerprint fp;
    for (const auto& raw : raw_entries) {
        const auto entry = normalize_entry(raw);
        if (entry.empty()) {
            continue;
        }
        if (is_loopback_address(entry)) {
            continue;
        }
        fp.entries.push_back(entry);
    }
    std::sort(fp.entries.begin(), fp.entries.end());
    fp.entries.erase(std::unique(fp.entries.begin(), fp.entries.end()), fp.entries.end());
    return fp;
}

bool fingerprint_changed(const IpFingerprint& previous, const IpFingerprint& current) {
    return previous.entries != current.entries;
}

bool is_box_claude_process(std::string_view name) {
    const auto base = basename_of(name);
    return base == "claude-desktop" || base == "claude" || base == "Claude";
}

IpWatchConfig watch_config(const claudebox::config::BoxConfig& config) {
    IpWatchConfig w;
    w.armed = config.features.kill_on_local_ip_change;
    w.grace_seconds = config.features.kill_grace_seconds;
    w.interval_seconds = config.features.watch_interval_seconds;
    if (w.grace_seconds < 0) {
        w.errors.emplace_back("kill_grace_seconds must be >= 0");
    }
    if (w.interval_seconds < 1) {
        w.errors.emplace_back("watch_interval_seconds must be >= 1");
    }
    w.ok = w.errors.empty();
    return w;
}

std::string action_kind_name(IpActionKind kind) {
    switch (kind) {
        case IpActionKind::NoOp:
            return "noop";
        case IpActionKind::StopGateway:
            return "stop_gateway";
        case IpActionKind::PoliteClose:
            return "polite_close";
        case IpActionKind::WaitGrace:
            return "wait_grace";
        case IpActionKind::ForceKill:
            return "force_kill";
    }
    return "noop";
}

bool IpChangePlan::starts_gateway_disconnect() const {
    return std::any_of(actions.begin(), actions.end(), [](const IpAction& a) {
        return a.kind == IpActionKind::StopGateway && a.start_offset_seconds == 0;
    });
}

bool IpChangePlan::starts_polite_close() const {
    return std::any_of(actions.begin(), actions.end(), [](const IpAction& a) {
        return a.kind == IpActionKind::PoliteClose && a.start_offset_seconds == 0;
    });
}

bool IpChangePlan::gateway_disconnect_delayed_until_grace() const {
    return std::any_of(actions.begin(), actions.end(), [&](const IpAction& a) {
        return a.kind == IpActionKind::StopGateway && a.start_offset_seconds >= grace_seconds &&
               grace_seconds > 0;
    });
}

IpChangePlan plan_ip_change(const claudebox::config::BoxConfig& config,
                            const IpFingerprint& previous,
                            const IpFingerprint& current,
                            const std::vector<BoxProcess>& processes,
                            bool gateway_running) {
    IpChangePlan plan;
    const auto watch = watch_config(config);
    plan.armed = watch.armed;
    plan.grace_seconds = watch.grace_seconds;
    plan.interval_seconds = watch.interval_seconds;
    plan.previous = previous;
    plan.current = current;
    plan.gateway_running = gateway_running;
    plan.fingerprint_changed = fingerprint_changed(previous, current);
    if (!watch.ok) {
        plan.ok = false;
        plan.errors = watch.errors;
        plan.noop = true;
        plan.reason = watch.errors.empty() ? "invalid watch config" : watch.errors.front();
        return plan;
    }

    std::vector<BoxProcess> in_scope;
    for (const auto& proc : processes) {
        if (is_box_claude_process(proc.name) && proc.alive) {
            in_scope.push_back(proc);
        }
    }
    plan.processes = in_scope;

    if (!plan.armed) {
        plan.noop = true;
        plan.reason = "kill_on_local_ip_change is off (default); no gateway stop, no process kill";
        return plan;
    }
    if (!plan.fingerprint_changed) {
        plan.noop = true;
        plan.reason = "local IP set unchanged; no gateway stop, no process kill";
        return plan;
    }

    plan.noop = false;
    plan.reason =
        "local IP set changed; start gateway disconnect and polite process close in parallel";

    IpAction stop;
    stop.kind = IpActionKind::StopGateway;
    stop.thread = IpActionThread::Gateway;
    stop.start_offset_seconds = 0;
    stop.parallel_group = 0;
    stop.label =
        "disconnect unique gateway immediately (parallel with polite close; do not wait for "
        "process grace)";
    plan.actions.push_back(stop);

    IpAction polite;
    polite.kind = IpActionKind::PoliteClose;
    polite.thread = IpActionThread::Process;
    polite.start_offset_seconds = 0;
    polite.parallel_group = 0;
    polite.targets = in_scope;
    polite.label =
        "polite close box Claude processes immediately (parallel with gateway disconnect)";
    plan.actions.push_back(polite);

    IpAction wait;
    wait.kind = IpActionKind::WaitGrace;
    wait.thread = IpActionThread::Process;
    wait.start_offset_seconds = 0;
    wait.parallel_group = -1;
    wait.label = "wait " + std::to_string(plan.grace_seconds) +
                 "s for processes only (gateway disconnect is not delayed)";
    plan.actions.push_back(wait);

    IpAction force;
    force.kind = IpActionKind::ForceKill;
    force.thread = IpActionThread::Process;
    force.start_offset_seconds = plan.grace_seconds;
    force.parallel_group = -1;
    force.targets = in_scope;
    force.label = "force kill box Claude processes still alive after grace";
    plan.actions.push_back(force);

    return plan;
}

IpChangeDryRun dry_run_ip_change(const claudebox::config::BoxConfig& config,
                                 const IpFingerprint& previous,
                                 const IpFingerprint& current,
                                 const std::vector<BoxProcess>& processes,
                                 bool gateway_running) {
    IpChangeDryRun run;
    run.plan = plan_ip_change(config, previous, current, processes, gateway_running);
    run.ok = run.plan.ok;
    run.processes_after = processes;
    bool gateway = gateway_running;

    if (!run.plan.ok || run.plan.noop) {
        run.gateway_stopped = false;
        run.log.push_back("noop: " + run.plan.reason);
        return run;
    }

    // Record the two immediate actions as starting together. The dry-run
    // does not wait for one before starting the other.
    for (const auto& action : run.plan.actions) {
        if (action.kind == IpActionKind::StopGateway && action.start_offset_seconds == 0) {
            run.log.push_back(
                "t=0 thread=gateway start: stop unique gateway (parallel; not waiting for processes)");
            gateway = false;
        }
        if (action.kind == IpActionKind::PoliteClose && action.start_offset_seconds == 0) {
            std::ostringstream line;
            line << "t=0 thread=process start: polite close";
            if (action.targets.empty()) {
                line << " (no box Claude processes in scope)";
            } else {
                line << " ";
                for (size_t i = 0; i < action.targets.size(); ++i) {
                    if (i) {
                        line << ", ";
                    }
                    line << action.targets[i].name << " pid=" << action.targets[i].pid;
                }
            }
            line << " (parallel; not waiting for gateway disconnect)";
            run.log.push_back(line.str());
        }
        if (action.kind == IpActionKind::WaitGrace) {
            run.log.push_back("t=0.." + std::to_string(run.plan.grace_seconds) +
                              " thread=process: wait grace for processes only");
        }
        if (action.kind == IpActionKind::ForceKill) {
            std::ostringstream line;
            line << "t=" << action.start_offset_seconds
                 << " thread=process: force kill remaining box Claude processes";
            run.log.push_back(line.str());
            for (auto& proc : run.processes_after) {
                if (is_box_claude_process(proc.name)) {
                    proc.alive = false;
                }
            }
        }
    }
    run.gateway_stopped = !gateway && gateway_running;
    if (!gateway_running) {
        run.log.push_back("note: gateway was already stopped; disconnect still requested immediately");
        run.gateway_stopped = true;  // requested / ensured stopped
    }
    return run;
}

std::string format_ip_change_plan(const IpChangePlan& plan, const IpChangeDryRun* run) {
    std::ostringstream out;
    out << "这是唯一网关模块里的本机 IP 波动响应（dry-run）。\n";
    out << "默认关闭。打开后，一旦宿主本机 IP 集合变化，"
           "断开网关和礼貌关闭盒子内 Claude 进程会作为两条并行线程同时启动，"
           "互不等待。进程在 grace（默认 10 秒）后若仍存活再强杀；"
           "网关断开不会等到这段 grace。\n";
    out << "不会在这台云电脑上启动或停止真正的 gost，也不会调用 wsl.exe 或结束真实进程。\n";
    if (!plan.ok) {
        out << "计划未通过：\n";
        for (const auto& err : plan.errors) {
            out << "  - " << err << "\n";
        }
        return out.str();
    }
    out << "策略开关：" << (plan.armed ? "已打开" : "关闭（默认）") << "\n";
    out << "进程 grace：" << plan.grace_seconds << " 秒\n";
    out << "监视间隔：" << plan.interval_seconds << " 秒\n";
    out << "上一组地址：\n";
    if (plan.previous.entries.empty()) {
        out << "  （空）\n";
    } else {
        for (const auto& e : plan.previous.entries) {
            out << "  " << e << "\n";
        }
    }
    out << "当前地址：\n";
    if (plan.current.entries.empty()) {
        out << "  （空）\n";
    } else {
        for (const auto& e : plan.current.entries) {
            out << "  " << e << "\n";
        }
    }
    out << "地址是否变化：" << (plan.fingerprint_changed ? "是" : "否") << "\n";
    out << "网关（模拟）状态：" << (plan.gateway_running ? "运行中" : "未运行") << "\n";
    out << "范围内的盒子 Claude 进程：";
    if (plan.processes.empty()) {
        out << "无\n";
    } else {
        out << "\n";
        for (const auto& proc : plan.processes) {
            out << "  " << proc.name << " pid=" << proc.pid << "\n";
        }
    }
    if (plan.noop) {
        out << "动作：无（" << plan.reason << "）\n";
    } else {
        out << "动作（并行启动，不是「先断开网关再礼貌关」）：\n";
        for (const auto& action : plan.actions) {
            out << "  t=" << action.start_offset_seconds << "s ";
            if (action.thread == IpActionThread::Gateway) {
                out << "thread=gateway ";
            } else if (action.thread == IpActionThread::Process) {
                out << "thread=process ";
            }
            if (action.parallel_group == 0) {
                out << "[并行组 0] ";
            }
            out << action_kind_name(action.kind) << " — " << action.label << "\n";
        }
    }
    if (run) {
        out << "dry-run 记录：\n";
        for (const auto& line : run->log) {
            out << "  " << line << "\n";
        }
    }
    return out.str();
}

}  // namespace claudebox::gateway
