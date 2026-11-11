#!/usr/bin/env bash
# Publish allowed host manufacturer + product into the guest presentation layer.
# Drops hardware identifiers. Does not rewrite live /sys/class/dmi (needs
# privileges / is kernel-backed; this layer is the contract Desktop/verify read).
# Idempotent. Not a VM-hiding tool.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PREFIX="${CLAUDE_BOX_PREFIX:-/opt/claude-box}"
PY="$SCRIPT_DIR/lib/host-identity.py"

if [[ ! -f "$PY" ]]; then
  echo "error: missing $PY" >&2
  exit 1
fi

# hide_identifiers is a hard constraint (default true).
case "${CLAUDE_BOX_HIDE_IDENTIFIERS:-true}" in
  true|True|TRUE|1|yes|Yes|on|On) ;;
  *)
    echo "error: host_identity.hide_identifiers must stay true (got ${CLAUDE_BOX_HIDE_IDENTIFIERS})" >&2
    exit 1
    ;;
esac

mkdir -p "$PREFIX/env" "$PREFIX/var" "$PREFIX/presentation/dmi/id"
exec python3 "$PY" apply "$PREFIX"
