#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace claudebox::config {

struct BaseSpec {
    std::string preferred_source = "Ubuntu-24.04";
    std::string install_dir;

    bool operator==(const BaseSpec&) const = default;
};

struct GatewayLogging {
    bool enabled = true;
    std::string path;

    bool operator==(const GatewayLogging&) const = default;
};

// Default unique-gateway blocklist (RE §4 / SCOPE).
// Host suffixes use the same rules as Test-ClaudeBoxGatewayHostBlocked:
// "sentry.io" matches sentry.io and o1158394.ingest.us.sentry.io.
// Entries that contain '/' are path-level (C++ request_blocked / gost hopGroup);
// they must not become a host-wide deny of api.anthropic.com.
inline std::vector<std::string> default_gateway_blocklist() {
    return {
        "sentry.io",
        "browser-intake-datadoghq.com",
        "browser-intake-us3-datadoghq.com",
        "browser-intake-us5-datadoghq.com",
        "browser-intake-ap1-datadoghq.com",
        "browser-intake-ap2-datadoghq.com",
        "browser-intake-datadoghq.eu",
        "browser-intake-ddog-gov.com",
        "api.anthropic.com/api/event_logging",
        "/api/event_logging",
        "/api/v2/rum",
    };
}

struct GatewaySpec {
    std::string host = "auto";
    int http_port = 3080;
    std::string upstream_http = "http://127.0.0.1:7890";
    std::string upstream_socks5 = "socks5://127.0.0.1:7891";
    std::vector<std::string> blocklist = default_gateway_blocklist();
    GatewayLogging logging;

    bool operator==(const GatewaySpec&) const = default;
};

struct VisibleDir {
    std::string host_path;
    std::string guest_path;
    bool read_only = false;

    bool operator==(const VisibleDir&) const = default;
};

struct MountSpec {
    bool disable_automount = true;

    bool operator==(const MountSpec&) const = default;
};

// Box-only spoof values. Not host timezone/language inventory.
struct IdentitySpoof {
    std::string timezone = "America/Los_Angeles";
    std::string locale = "en_US.UTF-8";
    std::string language = "en_US";
    std::string hostname = "claude-box";
    std::string electron_lang = "en-US";

    bool operator==(const IdentitySpoof&) const = default;
};

struct HostIdentitySpec {
    bool expose_real_model = true;
    std::string manufacturer;
    std::string product;
    // Transitional constraint: must remain true. Closing it is not a
    // "publish the whole inventory" switch; per-field policy is how a
    // user exposes or overrides a field.
    bool hide_identifiers = true;
    std::vector<std::string> hidden_identifier_keys = {
        "serial",
        "serial_number",
        "chassis_uuid",
        "board_uuid",
        "product_uuid",
        "mac",
        "mac_address",
        "disk_serial",
        "cpu_id",
        "processor_id",
        "machine_guid",
        "device_instance_path",
    };

    bool operator==(const HostIdentitySpec&) const = default;
};

struct DesktopSpec {
    std::string deb_url;
    std::string deb_local;
    std::string command = "claude-desktop";
    std::vector<std::string> extra_args;
    bool asar_patch = false;

    bool operator==(const DesktopSpec&) const = default;
};

struct FeaturesSpec {
    bool exclude_host_system_drive = true;
    bool computer_use = false;
    bool kill_on_local_ip_change = false;
    int kill_grace_seconds = 10;
    int watch_interval_seconds = 5;
    bool oauth_open_in_fingerprint_browser = true;
    bool identity_vault = true;
    // Empty = <prefix>/var/identity-vault. May also be a documented
    // convention path. Host %APPDATA%\Claude and browser profiles are rejected.
    std::string vault_dir;

    bool operator==(const FeaturesSpec&) const = default;
};

struct BoxConfig {
    std::string api_version = "claude-box/wsl/v1alpha1";
    std::string kind = "WslBox";
    std::string name = "default";
    std::string distro_name = "ClaudeBox";
    BaseSpec base;
    GatewaySpec gateway;
    std::vector<VisibleDir> visible_dirs = {VisibleDir{
        "D:\\Projects\\demo",
        "/mnt/claude-box/demo",
        false,
    }};
    MountSpec mount;
    IdentitySpoof identity;
    HostIdentitySpec host_identity;
    DesktopSpec desktop;
    FeaturesSpec features;

    bool operator==(const BoxConfig&) const = default;

    static BoxConfig defaults();
    static BoxConfig parse_yaml(std::string_view text);
    static BoxConfig load_file(const std::filesystem::path& path);

    std::string to_yaml() const;
    void save_file(const std::filesystem::path& path) const;
};

std::filesystem::path default_config_path(const std::filesystem::path& prefix);

// Refuse saving over the in-repo example file.
bool is_example_config_path(const std::filesystem::path& path);

}  // namespace claudebox::config
