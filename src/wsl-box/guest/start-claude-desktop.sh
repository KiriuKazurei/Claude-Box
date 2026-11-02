#!/usr/bin/env bash
# Start Claude Desktop with spoof env already injected by host Start-ClaudeBoxWsl.ps1.
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")" && pwd)"
# shellcheck source=/dev/null
source "$ROOT_DIR/apply-spoof.sh"

CMD="${CLAUDE_BOX_DESKTOP_CMD:-claude-desktop}"
EXTRA=()
if [[ -n "${CLAUDE_BOX_ELECTRON_LANG:-}" ]]; then
  EXTRA+=(--lang="${CLAUDE_BOX_ELECTRON_LANG}")
fi

echo "[claude-box] proxies: HTTP_PROXY=${HTTP_PROXY:-} HTTPS_PROXY=${HTTPS_PROXY:-}"
echo "[claude-box] starting: $CMD ${EXTRA[*]:-}"

if ! command -v "$CMD" >/dev/null 2>&1; then
  # Common alternative names after deb install
  for c in claude-desktop claude Claude; do
    if command -v "$c" >/dev/null 2>&1; then CMD="$c"; break; fi
  done
fi

if ! command -v "$CMD" >/dev/null 2>&1; then
  echo "error: Desktop binary not found. Run install-claude-desktop.sh first."
  exit 127
fi

# Gateway tips (non-fatal)
if [[ -z "${HTTP_PROXY:-}${ALL_PROXY:-}" ]]; then
  echo "warn: no HTTP_PROXY/ALL_PROXY — traffic may bypass Claude Box gateway"
fi

exec "$CMD" "${EXTRA[@]}" "$@"
