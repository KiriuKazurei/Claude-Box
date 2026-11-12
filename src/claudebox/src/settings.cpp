#include "claudebox/settings.hpp"
#include "claudebox/collect.hpp"
#include "claudebox/mount.hpp"
#include "claudebox/gateway.hpp"


#include <stdexcept>

namespace claudebox::settings {
namespace {

using claudebox::config::BoxConfig;
using claudebox::config::default_config_path;
using claudebox::config::is_example_config_path;
using claudebox::identity::ApplyResult;
using claudebox::identity::FieldKind;
using claudebox::identity::FieldMode;
using claudebox::identity::Inventory;
using claudebox::identity::Policy;
using claudebox::identity::apply_policy;
using claudebox::identity::load_inventory_file;
using claudebox::identity::policy_path;
using claudebox::identity::raw_inventory_path;
using claudebox::identity::verify;
using claudebox::identity::write_presentation;

}  // namespace

Paths resolve_paths(const Options& options) {
    Paths p;
    p.prefix = options.prefix;
    if (p.prefix.empty()) {
        throw std::runtime_error("settings prefix is required");
    }
    p.config = options.config.empty() ? default_config_path(p.prefix) : options.config;
    p.inventory = options.inventory.empty() ? raw_inventory_path(p.prefix) : options.inventory;
    p.policy = policy_path(p.prefix);
    return p;
}

Session Session::load(const Options& options) {
    Session s;
    s.paths = resolve_paths(options);
    std::filesystem::create_directories(s.paths.prefix);

    if (std::filesystem::exists(s.paths.config)) {
        s.config = BoxConfig::load_file(s.paths.config);
    } else {
        s.config = BoxConfig::defaults();
    }
    // The settings page never treats hide_identifiers as a user toggle.
    s.config.host_identity.hide_identifiers = true;

    if (std::filesystem::exists(s.paths.inventory)) {
        s.inventory = load_inventory_file(s.paths.inventory);
    } else {
        s.inventory = {};
    }

    if (std::filesystem::exists(s.paths.policy)) {
        s.policy = Policy::load_file(s.paths.policy);
    } else {
        s.policy = Policy::defaults();
    }

    const ApplyResult applied = apply_policy(s.inventory, s.policy);
    if (applied.ok) {
        s.presentation = applied.presentation;
    }
    return s;
}

void Session::reload() {
    Options opt;
    opt.prefix = paths.prefix;
    opt.config = paths.config;
    opt.inventory = paths.inventory;
    *this = Session::load(opt);
}

void Session::set_field_mode(std::string_view canonical, FieldMode mode,
                             std::string override_value) {
    const std::string id(canonical);
    auto fp = policy.policy_for(id);
    fp.mode = mode;
    if (mode == FieldMode::Override) {
        fp.override_value = std::move(override_value);
    } else if (mode != FieldMode::Override) {
        // Keep a leftover value only as a draft; apply ignores it unless override.
        if (mode != FieldMode::Override) {
            // Leave override_value as-is if the user might switch back; for
            // hide/expose we clear so a later save does not look like override.
            fp.override_value.clear();
        }
    }
    policy.fields[id] = std::move(fp);
}

SaveResult Session::save() {
    SaveResult result;
    try {
        if (is_example_config_path(paths.config)) {
            result.error =
                "refusing to overwrite example config: " + paths.config.string() +
                " (copy it to the box prefix first)";
            return result;
        }
        config.host_identity.hide_identifiers = true;
        const auto mount_plan = claudebox::mount::plan_mounts(config);
        if (!mount_plan.ok) {
            result.error = mount_plan.errors.empty()
                               ? "mount plan rejected the configuration"
                               : mount_plan.errors.front();
            for (size_t i = 1; i < mount_plan.errors.size(); ++i) {
                result.error.append("\n");
                result.error.append(mount_plan.errors[i]);
            }
            return result;
        }
        const auto gateway = claudebox::gateway::validate(config, paths.prefix);
        if (!gateway.ok) {
            result.error = gateway.errors.empty()
                               ? "gateway configuration was rejected"
                               : gateway.errors.front();
            for (size_t i = 1; i < gateway.errors.size(); ++i) {
                result.error.append("\n");
                result.error.append(gateway.errors[i]);
            }
            return result;
        }
        std::filesystem::create_directories(paths.prefix / "var");
        std::filesystem::create_directories(paths.config.parent_path());
        config.save_file(paths.config);
        policy.save_file(paths.policy);

        const ApplyResult applied = apply_policy(inventory, policy);
        if (!applied.ok) {
            result.error = applied.error.empty() ? "apply_policy failed" : applied.error;
            return result;
        }
        presentation = applied.presentation;
        write_presentation(paths.prefix, presentation);
        const auto issues = verify(inventory, policy, presentation);
        if (!issues.empty()) {
            result.error = issues.front().message;
            return result;
        }
        result.ok = true;
        return result;
    } catch (const std::exception& ex) {
        result.error = ex.what();
        return result;
    }
}

std::string field_label_zh(std::string_view canonical) {
    if (canonical == "manufacturer") return "厂商";
    if (canonical == "product") return "产品型号";
    if (canonical == "serial_number") return "序列号";
    if (canonical == "chassis_uuid") return "机箱 UUID";
    if (canonical == "board_uuid") return "主板 UUID";
    if (canonical == "product_uuid") return "产品 UUID";
    if (canonical == "mac_address") return "MAC 地址";
    if (canonical == "disk_serial") return "磁盘序列号";
    if (canonical == "cpu_id") return "CPU ID";
    if (canonical == "machine_guid") return "Windows MachineGuid";
    if (canonical == "device_instance_path") return "设备实例路径";
    if (canonical == "asset_tag") return "资产标签";
    return std::string(canonical);
}

std::string field_kind_label_zh(FieldKind kind) {
    switch (kind) {
        case FieldKind::Model:
            return "型号信息";
        case FieldKind::Identifier:
            return "硬件编码";
        case FieldKind::Unknown:
            return "其他";
    }
    return "其他";
}

std::string field_mode_label_zh(FieldMode mode) {
    switch (mode) {
        case FieldMode::Expose:
            return "原样展示";
        case FieldMode::Hide:
            return "隐藏";
        case FieldMode::Override:
            return "修改后展示";
    }
    return "隐藏";
}

}  // namespace claudebox::settings
