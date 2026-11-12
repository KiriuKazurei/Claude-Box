#pragma once

#include "claudebox/config.hpp"

#include <filesystem>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace claudebox::companion {

// Planned companion fingerprint browser launch. Never falls back to the
// host default browser (xdg-open / chrome / msedge / www-browser as host).
struct CompanionPlan {
    bool ok = false;
    std::vector<std::string> errors;
    bool feature_enabled = true;
    bool engine_ready = false;
    bool proxy_configured = false;
    // Always false: this planner will not open a host browser, even on error.
    bool would_use_host_browser = false;
    std::filesystem::path engine;
    std::filesystem::path profile_dir;
    std::filesystem::path intercept_dir;
    std::filesystem::path xdg_open;
    std::filesystem::path www_browser;
    std::filesystem::path open_oauth;
    std::filesystem::path companion_env;
    std::string timezone;
    std::string locale;
    std::string language;
    std::string electron_lang;
    std::string http_proxy;
    std::string https_proxy;
    std::string all_proxy;
    std::string no_proxy = "localhost,127.0.0.1";
    std::string url;
    std::vector<std::string> argv;
    std::vector<std::pair<std::string, std::string>> env;
    std::string command_line;
};

struct OpenResult {
    bool ok = false;
    int exit_code = 1;
    std::string error;
    std::string output;
    CompanionPlan plan;
    // Always false: a failure never opens the host default browser instead.
    bool launched_host_browser = false;
};

struct InterceptResult {
    bool ok = false;
    std::string error;
    CompanionPlan plan;
};

// Isolated profile under the box prefix. Not a host Chrome/Edge or Claude
// Electron userData path.
std::filesystem::path default_profile_dir(const std::filesystem::path& prefix);
// Optional user-supplied engine location inside the prefix. This tree is
// not populated automatically and must not be committed.
std::filesystem::path default_engine_path(const std::filesystem::path& prefix);
std::filesystem::path intercept_bin_dir(const std::filesystem::path& prefix);
std::filesystem::path companion_env_path(const std::filesystem::path& prefix);

// Client-side proxy URL for the unique gateway. "auto" / 0.0.0.0 listen
// addresses become 127.0.0.1 so the companion process can actually connect.
std::string gateway_proxy_url(const claudebox::config::BoxConfig& config);

bool is_http_url(std::string_view url);
// Typical Claude / Anthropic authorize, login, or callback URLs.
bool is_oauth_url(std::string_view url);

// True when the path looks like a host Chrome/Edge/Chromium profile or a
// Claude Electron userData directory — those must not be used as the
// companion profile.
bool is_forbidden_profile_path(const std::filesystem::path& path,
                               const std::filesystem::path& prefix = {});

// True when the path is a host default browser (system chrome/msedge/
// xdg-open, etc.), not a dedicated companion engine.
bool is_host_default_browser(const std::filesystem::path& path,
                             const std::filesystem::path& prefix = {});

// Resolve the engine: CLAUDE_BOX_COMPANION_BROWSER, then
// <prefix>/opt/companion-browser/engine. Never searches PATH for a host
// browser. An empty path means the engine is not ready.
std::filesystem::path resolve_engine(const std::filesystem::path& prefix);
std::filesystem::path resolve_profile(const std::filesystem::path& prefix);

// Dry-run plan. url may be empty when the caller only wants status.
// ok is true only when the feature is on, the engine is ready, the unique
// gateway proxy is configured, and (if given) the URL is http(s).
CompanionPlan plan(const claudebox::config::BoxConfig& config,
                   const std::filesystem::path& prefix,
                   std::string_view url = {});

// Write prefix/bin intercept wrappers (xdg-open, www-browser, open-oauth-url)
// and env/companion.env (BROWSER + PATH prepend). Does not require a ready
// engine — missing engines still refuse at open time, without host fallback.
// Never changes the host default browser.
InterceptResult install_intercept(const claudebox::config::BoxConfig& config,
                                  const std::filesystem::path& prefix);

// Open an http(s) URL in the companion engine. When dry_run is true, only
// the plan is produced. Missing engine / disabled feature / missing proxy
// all fail with a clear error and never fall back to a host browser.
OpenResult open_url(const claudebox::config::BoxConfig& config,
                    const std::filesystem::path& prefix,
                    std::string_view url,
                    bool dry_run = false);

// Chinese summary for the settings page. States clearly this is a plan /
// dry-run and that a missing engine will not fall back to the host browser.
std::string format_status(const CompanionPlan& plan);

}  // namespace claudebox::companion
