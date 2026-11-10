#!/usr/bin/env bash
# Open an OAuth / authorization URL in the Claude Box companion fingerprint browser.
# Never fall back to the host default browser (that would leak cookies / extensions / real fingerprint).
set -euo pipefail

usage() {
  cat <<'MSG'
Usage: open-oauth-url.sh <url>

Policy: Claude OAuth (OA) / authorize pages must open in the companion
fingerprint browser, not the host Chrome/Edge. See
docs/companion-fingerprint-browser.md.

This is a stub until W6 wires interception (xdg-open / BROWSER / openExternal)
and an engine (isolated Chromium profile, optional Fingerprint Chromium).
MSG
}

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
  usage
  exit 0
fi

URL="${1:-}"
if [[ -z "$URL" ]]; then
  usage >&2
  exit 2
fi

# Very small safety: only http(s)
if [[ ! "$URL" =~ ^https?:// ]]; then
  echo "error: refusing non-http(s) URL" >&2
  exit 2
fi

ENGINE="${CLAUDE_BOX_COMPANION_BROWSER:-}"
PROFILE="${CLAUDE_BOX_COMPANION_PROFILE:-}"

if [[ -z "$ENGINE" || ! -x "$ENGINE" ]]; then
  echo "error: companion fingerprint browser is not installed or not executable." >&2
  echo "  CLAUDE_BOX_COMPANION_BROWSER=${ENGINE:-<(unset)>}" >&2
  echo "  Refusing to open the host default browser (xdg-open/chrome/msedge)." >&2
  echo "  Set CLAUDE_BOX_COMPANION_BROWSER to an isolated Chromium (dedicated profile)," >&2
  echo "  then retry. Policy: docs/companion-fingerprint-browser.md" >&2
  exit 3
fi

args=()
if [[ -n "$PROFILE" ]]; then
  args+=(--user-data-dir="$PROFILE")
fi
echo "[claude-box] companion-browser: $ENGINE ${args[*]:-} $URL"
exec "$ENGINE" "${args[@]}" "$URL"
