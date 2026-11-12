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
  src/wsl-box/guest/apply-host-identity.sh
  src/wsl-box/guest/verify-host-identity.sh
  src/wsl-box/guest/lib/host-identity.py
  src/wsl-box/guest/lib/identity-engine.sh
  src/wsl-box/host/Export-ClaudeBoxHostIdentity.ps1
  src/wsl-box/linux/Install-ClaudeBoxFramework.sh
  tests/wsl-box/fixtures/host-inventory.raw.yaml
  tests/wsl-box/test-host-identity.sh
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

grep -q 'hide_identifiers:\s*true' src/wsl-box/config/wsl-box.example.yaml \
  || fail "example yaml must set hide_identifiers: true"
grep -q 'expose_real_model:\s*true' src/wsl-box/config/wsl-box.example.yaml \
  || fail "example yaml must set expose_real_model: true"
pass "hide_identifiers default true"

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

# Host identity: model visible, identifiers hidden, TZ still spoofed
bash tests/wsl-box/test-host-identity.sh

# start-claude-desktop.sh must refuse to exec when the unique-gateway proxy is missing
# (same contract as C++ desktop::start).
refuse_dir="$(mktemp -d "${ROOT}/.run/claude-box-refuse-XXXX")"
mkdir -p "$refuse_dir/env" "$refuse_dir/bin"
printf 'export CLAUDE_BOX_PREFIX=%q\nexport TZ=America/Los_Angeles\n' "$refuse_dir" > "$refuse_dir/env/session.env"
cat > "$refuse_dir/bin/claude-desktop" <<'STUB'
#!/bin/sh
echo "STUB_DESKTOP_EXECUTED" >> "${CLAUDE_BOX_PREFIX}/exec.log"
exit 0
STUB
chmod +x "$refuse_dir/bin/claude-desktop"
set +e
refuse_out="$(
  env -u HTTP_PROXY -u HTTPS_PROXY -u ALL_PROXY -u http_proxy -u https_proxy -u all_proxy \
    PATH="$refuse_dir/bin:$PATH" CLAUDE_BOX_PREFIX="$refuse_dir" CLAUDE_BOX_DESKTOP_CMD=claude-desktop \
    bash src/wsl-box/guest/start-claude-desktop.sh 2>&1
)"
refuse_rc=$?
set -e
if [[ "$refuse_rc" -eq 0 ]]; then
  rm -rf "$refuse_dir"
  fail "start-claude-desktop.sh exited 0 without a proxy"
fi
if [[ "$refuse_rc" -eq 127 ]]; then
  rm -rf "$refuse_dir"
  fail "start-claude-desktop.sh looked up the binary before refusing a missing proxy"
fi
echo "$refuse_out" | grep -qi 'proxy' || { rm -rf "$refuse_dir"; fail "missing-proxy error should mention proxy"; }
if [[ -f "$refuse_dir/exec.log" ]]; then
  rm -rf "$refuse_dir"
  fail "start-claude-desktop.sh exec'd Desktop without a proxy"
fi
rm -rf "$refuse_dir"
pass "start-claude-desktop.sh refuses missing proxy (no exec)"

echo "ALL GREEN (linux framework)"
