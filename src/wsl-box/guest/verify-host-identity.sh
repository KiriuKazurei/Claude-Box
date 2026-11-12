#!/usr/bin/env bash
# Confirm the presentation layer matches the identity policy and does not leak
# hidden values (including original values replaced by override).
# Delegates to the C++ claudebox-identity engine (P3).
# Reads the presentation layer, not live /sys/class/dmi.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PREFIX="${CLAUDE_BOX_PREFIX:-/opt/claude-box}"
# shellcheck source=lib/identity-engine.sh
source "$SCRIPT_DIR/lib/identity-engine.sh"

claudebox_require_hide_identifiers
BIN="$(claudebox_find_identity_bin "$SCRIPT_DIR")"
INVENTORY="$(claudebox_resolve_inventory "$PREFIX")"
POLICY="$(claudebox_resolve_policy "$PREFIX" || true)"

args=(verify --inventory "$INVENTORY" --prefix "$PREFIX")
if [[ -n "${POLICY:-}" ]]; then
  args+=(--policy "$POLICY")
fi

if ! "$BIN" "${args[@]}"; then
  echo "[claude-box] verify-host-identity FAIL" >&2
  exit 1
fi
echo "[claude-box] verify-host-identity OK"
if [[ -n "${POLICY:-}" ]]; then
  echo "[claude-box] policy=$POLICY"
else
  echo "[claude-box] policy=defaults; hidden identifier values not present in presentation"
fi
