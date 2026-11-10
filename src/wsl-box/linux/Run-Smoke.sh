#!/usr/bin/env bash
# Cloud Linux smoke: apply-spoof (session) → verify-spoof → gateway tips / blocklist.
# Does not start Claude Desktop. Does not touch Win11 WSL.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=lib/common.sh
source "$SCRIPT_DIR/lib/common.sh"
# shellcheck source=lib/load-config.sh
source "$SCRIPT_DIR/lib/load-config.sh"

PREFIX="${CLAUDE_BOX_PREFIX:-$(claude_box_default_prefix)}"
GUEST="${PREFIX}/guest"
ENV_FILE="${PREFIX}/env/session.env"

if [[ ! -d "$GUEST" || ! -f "$ENV_FILE" ]]; then
  claude_box_log "prefix not ready — running Install-ClaudeBoxFramework.sh first"
  "$SCRIPT_DIR/Install-ClaudeBoxFramework.sh"
fi

# shellcheck disable=SC1090
source "$ENV_FILE"

claude_box_log "smoke start prefix=$PREFIX"
claude_box_log "TZ=$TZ LANG=$LANG gateway=${CLAUDE_BOX_GATEWAY_HOST:-}:$CLAUDE_BOX_GATEWAY_PORT"
claude_box_log "features.kill_on_local_ip_change=${CLAUDE_BOX_KILL_ON_LOCAL_IP_CHANGE:-false}"
claude_box_log "features.oauth_open_in_fingerprint_browser=${CLAUDE_BOX_OAUTH_OPEN_IN_FINGERPRINT_BROWSER:-true}"
claude_box_log "features.identity_vault=${CLAUDE_BOX_IDENTITY_VAULT:-true}"

# 1) apply-spoof (session-level; /etc only if root)
bash "$GUEST/apply-spoof.sh"

# 2) verify
bash "$GUEST/verify-spoof.sh"

# 3) gateway tips (config-driven blocklist)
bash "$GUEST/gateway-tips.sh"

# 4) Assert Intl / date look like spoof (node present on this box)
if command -v node >/dev/null 2>&1; then
  node -e '
const o = Intl.DateTimeFormat().resolvedOptions();
const tz = process.env.TZ || "";
if (tz && o.timeZone !== tz) {
  console.error("FAIL Intl timeZone", o.timeZone, "!=", tz);
  process.exit(1);
}
console.log("[claude-box/linux] Intl OK timeZone=", o.timeZone, "locale=", o.locale);
'
fi

# 5) Blocklist non-empty from example config
if [[ -z "${CLAUDE_BOX_BLOCKLIST:-}" ]]; then
  claude_box_warn "CLAUDE_BOX_BLOCKLIST empty — check config blocklist"
else
  claude_box_log "blocklist=$CLAUDE_BOX_BLOCKLIST"
  # Expect at least one sentry/datadog-ish entry from example yaml
  case ",$CLAUDE_BOX_BLOCKLIST," in
    *sentry*|*datadog*|*amplitude*|*google-analytics*) ;;
    *) claude_box_warn "blocklist has no known telemetry host substring (ok if custom)" ;;
  esac
fi

# 6) kill_on_local_ip_change must default false
case "${CLAUDE_BOX_KILL_ON_LOCAL_IP_CHANGE:-false}" in
  false|False|FALSE|0|no|No|off|Off) ;;
  *) claude_box_die "kill_on_local_ip_change must default false (got ${CLAUDE_BOX_KILL_ON_LOCAL_IP_CHANGE})" ;;
esac

# 7) intended-on flags must default true in loaded example config
case "${CLAUDE_BOX_OAUTH_OPEN_IN_FINGERPRINT_BROWSER:-true}" in
  true|True|TRUE|1|yes|Yes|on|On) ;;
  *) claude_box_die "oauth_open_in_fingerprint_browser must default true (got ${CLAUDE_BOX_OAUTH_OPEN_IN_FINGERPRINT_BROWSER})" ;;
esac
case "${CLAUDE_BOX_IDENTITY_VAULT:-true}" in
  true|True|TRUE|1|yes|Yes|on|On) ;;
  *) claude_box_die "identity_vault must default true (got ${CLAUDE_BOX_IDENTITY_VAULT})" ;;
esac

# 8) OA opener stub must refuse host-browser fallback when engine is missing
stub="$(claude_box_repo_root)/src/wsl-box/companion-browser/open-oauth-url.sh"
if [[ -x "$stub" ]]; then
  if CLAUDE_BOX_COMPANION_BROWSER= bash "$stub" "https://claude.ai/login" >/dev/null 2>"$PREFIX/oauth-stub.err"; then
    claude_box_die "open-oauth-url.sh must fail when companion engine is unset"
  fi
  grep -q "Refusing to open the host default browser" "$PREFIX/oauth-stub.err"     || claude_box_die "open-oauth-url.sh must refuse host default browser"
  claude_box_log "companion OA stub refuses host fallback"
fi

claude_box_log "SMOKE OK"
