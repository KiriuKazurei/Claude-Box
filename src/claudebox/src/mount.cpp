#include "claudebox/mount.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace claudebox::mount {
namespace {

std::string to_lower_copy(std::string_view s) {
    std::string out(s);
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

std::string replace_slashes(std::string_view s) {
    std::string out(s);
    for (char& c : out) {
        if (c == '\\') {
            c = '/';
        }
    }
    return out;
}

std::string trim_copy(std::string_view s) {
    auto first = s.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos) {
        return {};
    }
    auto last = s.find_last_not_of(" \t\r\n");
    return std::string(s.substr(first, last - first + 1));
}

// Strip a Windows long-path prefix if present.
std::string strip_extended_prefix(std::string s) {
    const auto lower = to_lower_copy(s);
    if (lower.rfind("//?/", 0) == 0 || lower.rfind("\\\\?\\", 0) == 0) {
        s = s.substr(4);
    }
    if (to_lower_copy(s).rfind("//?/", 0) == 0) {
        s = s.substr(4);
    }
    return s;
}

std::vector<std::string> split_parts(const std::string& posix) {
    std::vector<std::string> raw;
    std::string cur;
    for (char c : posix) {
        if (c == '/') {
            if (!cur.empty()) {
                raw.push_back(cur);
                cur.clear();
            }
        } else {
            cur.push_back(c);
        }
    }
    if (!cur.empty()) {
        raw.push_back(cur);
    }
    std::vector<std::string> out;
    for (const auto& p : raw) {
        if (p == "." || p.empty()) {
            continue;
        }
        if (p == "..") {
            if (!out.empty()) {
                out.pop_back();
            }
            continue;
        }
        out.push_back(p);
    }
    return out;
}

struct ParsedPath {
    char drive = '\0';          // 'c' when this is a Windows drive or /mnt/c form
    std::vector<std::string> parts;  // lowercased, without the drive letter
    bool windows_like = false;
};

ParsedPath parse_path(std::string_view raw) {
    ParsedPath out;
    std::string s = replace_slashes(strip_extended_prefix(trim_copy(raw)));
    if (s.empty()) {
        return out;
    }
    // Drop a trailing slash except we still want "C:" to parse as a root.
    while (s.size() > 1 && s.back() == '/') {
        s.pop_back();
    }
    const std::string lower = to_lower_copy(s);

    // /mnt/c, /mnt/c/users/...
    auto from_mnt_drive = [&](const std::string& prefix) -> bool {
        if (lower == prefix) {
            out.windows_like = true;
            return true;
        }
        if (lower.rfind(prefix + "/", 0) == 0) {
            out.windows_like = true;
            out.parts = split_parts(lower.substr(prefix.size()));
            return true;
        }
        return false;
    };

    if (lower.size() >= 2 && std::isalpha(static_cast<unsigned char>(lower[0])) &&
        lower[1] == ':') {
        out.drive = lower[0];
        out.windows_like = true;
        std::string rest = (lower.size() > 2 && lower[2] == '/') ? lower.substr(3) : lower.substr(2);
        if (!rest.empty() && rest[0] == '/') {
            rest = rest.substr(1);
        }
        out.parts = split_parts(rest);
        return out;
    }

    if (lower.rfind("/mnt/", 0) == 0 && lower.size() >= 6 &&
        std::isalpha(static_cast<unsigned char>(lower[5])) &&
        (lower.size() == 6 || lower[6] == '/')) {
        out.drive = static_cast<char>(std::tolower(static_cast<unsigned char>(lower[5])));
        out.windows_like = true;
        out.parts = (lower.size() > 6) ? split_parts(lower.substr(6)) : std::vector<std::string>{};
        return out;
    }
    if (from_mnt_drive("/cygdrive/c")) {
        out.drive = 'c';
        return out;
    }

    // Bare posix (guest paths, etc.)
    out.parts = split_parts(lower);
    return out;
}

}  // namespace

bool is_system_drive_root(std::string_view host_path) {
    const auto p = parse_path(host_path);
    return p.drive == 'c' && p.parts.empty();
}

bool is_whole_users_tree(std::string_view host_path) {
    const auto p = parse_path(host_path);
    if (p.drive != 'c' || p.parts.empty() || p.parts[0] != "users") {
        return false;
    }
    // C:\Users and C:\Users\<profile> are overly broad. Deeper paths
    // such as C:\Users\Alice\Documents are not the whole tree.
    return p.parts.size() <= 2;
}

bool is_host_c_guest_path(std::string_view guest_path) {
    const auto p = parse_path(guest_path);
    if (p.drive == 'c') {
        return true;
    }
    // /mnt/c already sets drive. Also treat explicit leftover forms.
    const std::string lower = to_lower_copy(replace_slashes(trim_copy(guest_path)));
    return lower == "/mnt/c" || lower.rfind("/mnt/c/", 0) == 0;
}

bool is_allowed_guest_prefix(std::string_view guest_path) {
    const std::string lower = to_lower_copy(replace_slashes(trim_copy(guest_path)));
    if (lower.empty()) {
        return false;
    }
    const auto parts = split_parts(lower);
    // Must be /mnt/claude-box or /mnt/claude-box/<...>
    if (parts.size() < 2 || parts[0] != "mnt" || parts[1] != "claude-box") {
        return false;
    }
    return true;
}

bool is_forbidden_host_path(std::string_view host_path) {
    return is_system_drive_root(host_path) || is_whole_users_tree(host_path);
}

MountPlan plan_mounts(const claudebox::config::BoxConfig& config) {
    MountPlan plan;
    plan.disable_automount = config.mount.disable_automount;
    plan.exclude_host_system_drive = config.features.exclude_host_system_drive;
    plan.distro.distro_name = config.distro_name;
    plan.distro.preferred_source = config.base.preferred_source;
    plan.distro.install_dir = config.base.install_dir;
    plan.distro.disable_automount = config.mount.disable_automount;

    auto fail = [&](std::string message) { plan.errors.push_back(std::move(message)); };

    if (!config.features.exclude_host_system_drive) {
        fail("exclude_host_system_drive 必须保持为 true，不能关闭后整挂宿主系统盘。");
    }

    if (config.visible_dirs.empty()) {
        // An empty list is a valid plan: mount nothing besides the distro itself.
    }

    for (const auto& dir : config.visible_dirs) {
        const auto host = trim_copy(dir.host_path);
        const auto guest = trim_copy(dir.guest_path);
        if (host.empty()) {
            fail("可见目录缺少宿主路径。");
            continue;
        }
        if (guest.empty()) {
            fail("可见目录 " + host + " 缺少客机路径。");
            continue;
        }
        if (is_system_drive_root(host)) {
            fail("拒绝挂载宿主系统盘根：" + host);
            continue;
        }
        if (is_whole_users_tree(host)) {
            fail("拒绝挂载整棵 Users 目录或整个用户配置目录：" + host);
            continue;
        }
        if (is_host_c_guest_path(guest) || is_system_drive_root(guest)) {
            fail("拒绝把整个宿主 C: 挂到客机：" + guest);
            continue;
        }
        if (!is_allowed_guest_prefix(guest)) {
            fail("客机路径必须位于 /mnt/claude-box 下：" + guest);
            continue;
        }
        plan.mounts.push_back(MountEntry{host, guest, dir.read_only});
    }

    const auto& d = plan.distro;
    auto& steps = plan.distro.steps;
    steps.clear();
    steps.push_back("云电脑 dry-run：不会调用 wsl.exe，也不会启用真机 WSL。");
    steps.push_back("计划创建或导入发行版「" + (d.distro_name.empty() ? std::string("ClaudeBox") : d.distro_name) +
                    "」（来源：" +
                    (d.preferred_source.empty() ? std::string("(未指定)") : d.preferred_source) + "）。");
    if (d.install_dir.empty()) {
        steps.push_back("安装目录：宿主默认 %LOCALAPPDATA%\\ClaudeBox\\wsl\\" +
                        (d.distro_name.empty() ? std::string("ClaudeBox") : d.distro_name) + "。");
    } else {
        steps.push_back("安装目录：" + d.install_dir + "。");
    }
    if (d.disable_automount) {
        steps.push_back("写入 /etc/wsl.conf：automount.enabled=false，切断默认 /mnt/c。");
    } else {
        steps.push_back("配置未关闭 automount；建议保持 disable_automount: true。");
        fail("disable_automount 应为 true，以免发行版自动挂上 /mnt/c。");
    }
    steps.push_back("同步 guest 脚本到发行版 /opt/claude-box/guest（由现有 host 脚本在真机执行）。");
    if (plan.mounts.empty()) {
        steps.push_back("可见目录为空：不会 bind-mount 任何宿主路径。");
    } else {
        steps.push_back("只按 visible_dirs 做 bind-mount（共 " + std::to_string(plan.mounts.size()) + " 条）。");
    }

    plan.ok = plan.errors.empty();
    return plan;
}

std::string format_plan(const MountPlan& plan) {
    std::ostringstream out;
    out << "运行时预览（dry-run，不会调用 wsl.exe）\n";
    out << "发行版：" << (plan.distro.distro_name.empty() ? "ClaudeBox" : plan.distro.distro_name)
        << "\n";
    out << "disable_automount：" << (plan.disable_automount ? "true" : "false") << "\n";
    out << "exclude_host_system_drive：" << (plan.exclude_host_system_drive ? "true" : "false")
        << "\n\n";
    out << "发行版生命周期计划：\n";
    for (const auto& step : plan.distro.steps) {
        out << "  • " << step << "\n";
    }
    out << "\n挂载计划：\n";
    if (plan.mounts.empty()) {
        out << "  （无可见目录）\n";
    } else {
        for (const auto& m : plan.mounts) {
            out << "  • " << m.host_path << " → " << m.guest_path
                << (m.read_only ? " （只读）" : "") << "\n";
        }
    }
    if (!plan.errors.empty()) {
        out << "\n错误（保存会被拒绝）：\n";
        for (const auto& err : plan.errors) {
            out << "  • " << err << "\n";
        }
    } else {
        out << "\n校验通过。\n";
    }
    return out.str();
}

}  // namespace claudebox::mount
