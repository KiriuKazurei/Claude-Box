#pragma once

#include "claudebox/config.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace claudebox::mount {

struct MountEntry {
    std::string host_path;
    std::string guest_path;
    bool read_only = false;
};

struct DistroPlan {
    std::string distro_name;
    std::string preferred_source;
    std::string install_dir;
    bool disable_automount = true;
    // Human-readable planned steps. Never invokes wsl.exe.
    std::vector<std::string> steps;
};

struct MountPlan {
    bool ok = false;
    std::vector<std::string> errors;
    std::vector<MountEntry> mounts;
    bool disable_automount = true;
    bool exclude_host_system_drive = true;
    DistroPlan distro;
};

// True for host system-drive roots (C:, C:\, C:/) and equivalents such as /mnt/c.
bool is_system_drive_root(std::string_view host_path);

// True for C:\Users, C:\Users\, and a whole user-profile root C:\Users\<name>.
bool is_whole_users_tree(std::string_view host_path);

// True when a guest path would remount /mnt/c (or the whole host C:).
bool is_host_c_guest_path(std::string_view guest_path);

// Guest bind targets must stay under /mnt/claude-box.
bool is_allowed_guest_prefix(std::string_view guest_path);

// Combined host-path policy used when exclude_host_system_drive is enforced.
bool is_forbidden_host_path(std::string_view host_path);

// Build a dry-run mount + distro plan from box config. Does not call wsl.exe.
MountPlan plan_mounts(const claudebox::config::BoxConfig& config);

// Chinese summary for the settings page (includes errors when not ok).
std::string format_plan(const MountPlan& plan);

}  // namespace claudebox::mount
