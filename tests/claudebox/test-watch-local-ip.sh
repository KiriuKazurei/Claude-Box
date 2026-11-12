#!/usr/bin/env bash
# Dry-run checks for Watch-ClaudeBoxLocalIp.ps1 (unique-gateway IP response).
# Uses pwsh when present; never calls wsl.exe or stops a live gateway.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"
SCRIPT="$ROOT/src/wsl-box/host/Watch-ClaudeBoxLocalIp.ps1"
CFG="$ROOT/src/wsl-box/config/wsl-box.example.yaml"

fail() { echo "FAIL: $*" >&2; exit 1; }
pass() { echo "OK: $*"; }

[[ -f "$SCRIPT" ]] || fail "missing $SCRIPT"
grep -q 'Start-ParallelIpChangeResponse' "$SCRIPT" || fail "watcher must start the dual action via Start-ParallelIpChangeResponse"
grep -q 'Start-Job' "$SCRIPT" || fail "live path must start gateway disconnect without waiting (Start-Job)"
grep -q 'Stop-ClaudeBoxGateway.ps1' "$SCRIPT" || fail "watcher must call scripts/win11/Stop-ClaudeBoxGateway.ps1"
grep -q 'not waiting for process close' "$SCRIPT" || fail "dry-run copy must say gateway does not wait for process close"
grep -q 'not waiting for gateway disconnect' "$SCRIPT" || fail "dry-run copy must say polite close does not wait for gateway"
# Must not describe a serial prerequisite in the action banner.
if grep -q '先断开网关，再礼貌关' "$SCRIPT"; then
  fail "watcher must not use serial 'gateway then polite close' wording"
fi
pass "watcher stub has parallel-start contract"

if ! command -v pwsh >/dev/null 2>&1; then
  echo "NOTE: pwsh not found; skipped live dry-run invocation of Watch-ClaudeBoxLocalIp.ps1"
  exit 0
fi

PREV=$'Ethernet|192.168.1.20/24\nEthernet|fe80::1/64'
CUR=$'Ethernet|10.0.0.8/24\nEthernet|fe80::1/64'

off="$(pwsh -File "$SCRIPT" -ConfigPath "$CFG" -DryRun -Once \
  -PreviousFingerprint "$PREV" -CurrentFingerprint "$CUR" 2>&1)" || rc=$?
rc=${rc:-0}
[[ "$rc" -eq 0 ]] || fail "default-off watcher exited $rc"
echo "$off" | grep -q 'kill_on_local_ip_change is OFF' || fail "default off must refuse to arm: $off"
if echo "$off" | grep -q 'thread=gateway start now'; then
  fail "default off must not start gateway disconnect: $off"
fi
pass "default off exits 0 without acting"

on="$(pwsh -File "$SCRIPT" -ConfigPath "$CFG" -ForceEnable -DryRun -Once \
  -PreviousFingerprint "$PREV" -CurrentFingerprint "$CUR" 2>&1)" || rc=$?
rc=${rc:-0}
[[ "$rc" -eq 0 ]] || fail "armed dry-run exited $rc: $on"
echo "$on" | grep -q 'start gateway disconnect and polite close in parallel' \
  || fail "armed change must announce parallel start: $on"
echo "$on" | grep -q 'thread=gateway start now: Stop-ClaudeBoxGateway' \
  || fail "armed change must start gateway disconnect immediately: $on"
echo "$on" | grep -q 'thread=process start now: polite close' \
  || fail "armed change must start polite close immediately: $on"
echo "$on" | grep -q 'force kill remaining box Claude processes' \
  || fail "armed change must mention process force-kill after grace: $on"
pass "armed + IP change dry-run starts both threads immediately"

same="$(pwsh -File "$SCRIPT" -ForceEnable -DryRun -Once \
  -PreviousFingerprint "$CUR" -CurrentFingerprint "$CUR" 2>&1)" || rc=$?
rc=${rc:-0}
[[ "$rc" -eq 0 ]] || fail "unchanged dry-run exited $rc"
echo "$same" | grep -q 'local IP set unchanged' || fail "unchanged IPs must be a no-op: $same"
if echo "$same" | grep -q 'thread=gateway start now'; then
  fail "unchanged IPs must not stop the gateway: $same"
fi
pass "armed + unchanged IPs is a no-op"
