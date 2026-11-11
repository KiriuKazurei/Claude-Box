#!/usr/bin/env bash
# Confirm the presentation layer shows manufacturer/product and does not leak
# hardware identifiers. Also fails if hide_identifiers is not true, or if a
# raw host inventory/fixture serial still appears in advertised env/files.
# Reads the presentation layer, not live /sys/class/dmi.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PREFIX="${CLAUDE_BOX_PREFIX:-/opt/claude-box}"
PY="$SCRIPT_DIR/lib/host-identity.py"

if [[ ! -f "$PY" ]]; then
  echo "error: missing $PY" >&2
  exit 1
fi

case "${CLAUDE_BOX_HIDE_IDENTIFIERS:-true}" in
  true|True|TRUE|1|yes|Yes|on|On) ;;
  *)
    echo "error: host_identity.hide_identifiers must stay true (got ${CLAUDE_BOX_HIDE_IDENTIFIERS})" >&2
    exit 1
    ;;
esac

exec python3 "$PY" verify "$PREFIX"
