#!/usr/bin/env bash
# Structure + cloud-Linux smoke for src/wsl-box/linux framework.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"

fail() { echo "FAIL: $*" >&2; exit 1; }
pass() { echo "OK: $*"; }

need=(
  src/wsl-box/README.md
  src/wsl-box/config/wsl-box.example.yaml
  src/wsl-box/host/New-ClaudeBoxWsl.ps1
  src/wsl-box/host/Start-ClaudeBoxWsl.ps1
  src/wsl-box/host/Watch-ClaudeBoxLocalIp.ps1
  src/wsl-box/guest/apply-spoof.sh
  src/wsl-box/guest/verify-spoof.sh
  src/wsl-box/guest/gateway-tips.sh
  src/wsl-box/guest/install-claude-desktop.sh
  src/wsl-box/guest/start-claude-desktop.sh
  src/wsl-box/linux/Install-ClaudeBoxFramework.sh
  src/wsl-box/linux/Run-Smoke.sh
  src/wsl-box/linux/Install-ClaudeDesktop.sh
  src/wsl-box/linux/lib/common.sh
  src/wsl-box/linux/lib/load-config.sh
  docs/architecture-v2-wsl.md
  docs/companion-fingerprint-browser.md
  docs/identity-vault.md
  src/wsl-box/companion-browser/open-oauth-url.sh
  src/wsl-box/identity-vault/README.md
)

for f in "${need[@]}"; do
  [[ -f "$f" ]] || fail "missing $f"
done
pass "structure files present"

# kill_on_local_ip_change default false in example yaml
grep -q 'kill_on_local_ip_change:\s*false' src/wsl-box/config/wsl-box.example.yaml \
  || fail "example yaml must default kill_on_local_ip_change: false"
pass "kill_on_local_ip_change default false"

grep -q 'oauth_open_in_fingerprint_browser:\s*true' src/wsl-box/config/wsl-box.example.yaml \
  || fail "example yaml must default oauth_open_in_fingerprint_browser: true"
pass "oauth_open_in_fingerprint_browser default true"

grep -q 'identity_vault:\s*true' src/wsl-box/config/wsl-box.example.yaml \
  || fail "example yaml must default identity_vault: true"
pass "identity_vault default true"

# .run gitignored
grep -qE '^\.run/?$' .gitignore || fail ".run/ not in .gitignore"
pass ".run/ gitignored"

# bash -n all scripts
while IFS= read -r -d '' sh; do
  bash -n "$sh" || fail "bash -n $sh"
done < <(find src/wsl-box/guest src/wsl-box/linux src/wsl-box/companion-browser -name '*.sh' -print0)
pass "bash -n guest+linux+companion-browser"

# Config loader smoke
# shellcheck disable=SC1091
source src/wsl-box/linux/lib/load-config.sh
cfg=src/wsl-box/config/wsl-box.example.yaml
tz="$(yaml_scalar "$cfg" timezone)"
[[ "$tz" == "America/Los_Angeles" ]] || fail "timezone parse got '$tz'"
bl="$(yaml_blocklist "$cfg")"
echo "$bl" | grep -q sentry || fail "blocklist missing sentry"
pass "config loader"

# Framework install + smoke (project-local .run)
export CLAUDE_BOX_PREFIX="${ROOT}/.run/claude-box-test-$$"
trap 'rm -rf "$CLAUDE_BOX_PREFIX"' EXIT
src/wsl-box/linux/Install-ClaudeBoxFramework.sh
[[ -f "$CLAUDE_BOX_PREFIX/env/session.env" ]] || fail "session.env missing"
[[ -x "$CLAUDE_BOX_PREFIX/guest/verify-spoof.sh" ]] || fail "guest not synced"
src/wsl-box/linux/Run-Smoke.sh
pass "Install + Run-Smoke"

# Desktop --check (deb may or may not exist; both OK paths)
if ls installers/linux/*.deb >/dev/null 2>&1; then
  src/wsl-box/linux/Install-ClaudeDesktop.sh --check
  pass "Install-ClaudeDesktop --check (deb present)"
else
  if src/wsl-box/linux/Install-ClaudeDesktop.sh --check; then
    fail "expected exit 2 when deb missing"
  else
    rc=$?
    [[ "$rc" -eq 2 ]] || fail "expected exit 2, got $rc"
    pass "Install-ClaudeDesktop --check fails clearly without deb"
  fi
fi

echo "ALL GREEN (linux framework)"
