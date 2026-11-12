#!/usr/bin/env bash
# Publish policy-approved host fields into the guest presentation layer.
# Delegates generation to the C++ claudebox-identity engine (P3).
# Collect writes only the raw inventory; this script writes the presentation.
# Does not rewrite live /sys/class/dmi. Idempotent. Not a VM-hiding tool.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PREFIX="${CLAUDE_BOX_PREFIX:-/opt/claude-box}"
# shellcheck source=lib/identity-engine.sh
source "$SCRIPT_DIR/lib/identity-engine.sh"

claudebox_require_hide_identifiers
BIN="$(claudebox_find_identity_bin "$SCRIPT_DIR")"
INVENTORY="$(claudebox_resolve_inventory "$PREFIX")"
POLICY="$(claudebox_resolve_policy "$PREFIX" || true)"

mkdir -p "$PREFIX/env" "$PREFIX/var" "$PREFIX/presentation/dmi/id"

args=(apply --inventory "$INVENTORY" --prefix "$PREFIX")
if [[ -n "${POLICY:-}" ]]; then
  args+=(--policy "$POLICY")
fi

"$BIN" "${args[@]}"
echo "[claude-box] host-identity applied via $BIN"
if [[ -n "${POLICY:-}" ]]; then
  echo "[claude-box] policy=$POLICY"
else
  echo "[claude-box] policy=defaults (manufacturer/product may be shown; identifiers hidden)"
fi
echo "[claude-box] presentation → $PREFIX/presentation/dmi/id"
echo "[claude-box] note: live /sys/class/dmi is not rewritten"
