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

If the companion engine is not ready, this command fails clearly and
refuses to open the host default browser (xdg-open / chrome / msedge).
MSG
}

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
  usage
  exit 0
fi

# Accept the first http(s) argument (xdg-open-style wrappers may pass flags).
URL=""
for arg in "$@"; do
  case "$arg" in
    http://*|https://*)
      URL="$arg"
      break
      ;;
  esac
done
if [[ -z "$URL" && -n "${1:-}" ]]; then
  URL="$1"
fi
if [[ -z "$URL" ]]; then
  usage >&2
  exit 2
fi

if [[ ! "$URL" =~ ^https?:// ]]; then
  echo "error: refusing non-http(s) URL" >&2
  echo "  Refusing to open the host default browser (xdg-open/chrome/msedge)." >&2
  exit 2
fi

# Load box prefix env (spoof TZ/LANG and unique-gateway proxy) when present.
if [[ -n "${CLAUDE_BOX_PREFIX:-}" ]]; then
  if [[ -f "${CLAUDE_BOX_PREFIX}/env/spoof.env" ]]; then
    # shellcheck disable=SC1091
    set -a
    # shellcheck disable=SC1090
    source "${CLAUDE_BOX_PREFIX}/env/spoof.env"
    set +a
  fi
  if [[ -f "${CLAUDE_BOX_PREFIX}/env/session.env" ]]; then
    # shellcheck disable=SC1090
    source "${CLAUDE_BOX_PREFIX}/env/session.env" || true
  fi
  if [[ -f "${CLAUDE_BOX_PREFIX}/env/companion.env" ]]; then
    # shellcheck disable=SC1090
    source "${CLAUDE_BOX_PREFIX}/env/companion.env" || true
  fi
fi

flag="${CLAUDE_BOX_OAUTH_OPEN_IN_FINGERPRINT_BROWSER:-true}"
case "$flag" in
  true|True|TRUE|1|yes|Yes|on|On) ;;
  *)
    echo "error: features.oauth_open_in_fingerprint_browser is disabled." >&2
    echo "  Refusing to open the host default browser (xdg-open/chrome/msedge)." >&2
    exit 4
    ;;
esac

basename_of() {
  local p="$1"
  p="${p%/}"
  echo "${p##*/}"
}

is_host_default_browser() {
  local path="$1"
  local base
  base="$(basename_of "$path" | tr '[:upper:]' '[:lower:]')"
  case "$base" in
    xdg-open|x-www-browser|www-browser|gnome-www-browser|gio|gvfs-open|exo-open|open|google-chrome|google-chrome-stable|google-chrome-beta|chromium|chromium-browser|msedge|microsoft-edge|microsoft-edge-stable|firefox|firefox-bin|firefox-esr|brave-browser|opera)
      case "$path" in
        /usr/bin/*|/usr/local/bin/*|/bin/*|/snap/*|/mnt/c/*|[A-Za-z]:\\Program*|[A-Za-z]:/Program*)
          return 0
          ;;
      esac
      if [[ -n "${CLAUDE_BOX_PREFIX:-}" && "$path" == "${CLAUDE_BOX_PREFIX}/bin/"* ]]; then
        return 0
      fi
      ;;
  esac
  return 1
}

# Resolve the companion engine. Never search PATH for a host browser.
ENGINE="${CLAUDE_BOX_COMPANION_BROWSER:-}"
if [[ -z "$ENGINE" && -n "${CLAUDE_BOX_PREFIX:-}" && -x "${CLAUDE_BOX_PREFIX}/opt/companion-browser/engine" ]]; then
  ENGINE="${CLAUDE_BOX_PREFIX}/opt/companion-browser/engine"
fi

if [[ -z "$ENGINE" || ! -x "$ENGINE" ]] || is_host_default_browser "$ENGINE"; then
  echo "error: companion fingerprint browser is not installed or not executable." >&2
  echo "  CLAUDE_BOX_COMPANION_BROWSER=${ENGINE:-<(unset)>}" >&2
  echo "  Refusing to open the host default browser (xdg-open/chrome/msedge)." >&2
  echo "  Set CLAUDE_BOX_COMPANION_BROWSER to an isolated Chromium (dedicated profile)," >&2
  if [[ -n "${CLAUDE_BOX_PREFIX:-}" ]]; then
    echo "  or place an engine at ${CLAUDE_BOX_PREFIX}/opt/companion-browser/engine, then retry." >&2
  else
    echo "  then retry. Policy: docs/companion-fingerprint-browser.md" >&2
  fi
  echo "  Policy: docs/companion-fingerprint-browser.md" >&2
  exit 3
fi

PROFILE="${CLAUDE_BOX_COMPANION_PROFILE:-}"
if [[ -z "$PROFILE" && -n "${CLAUDE_BOX_PREFIX:-}" ]]; then
  PROFILE="${CLAUDE_BOX_PREFIX}/var/companion-browser/profile"
fi
if [[ -z "$PROFILE" ]]; then
  echo "error: companion profile is not set (CLAUDE_BOX_COMPANION_PROFILE or CLAUDE_BOX_PREFIX)." >&2
  echo "  Refusing to open the host default browser (xdg-open/chrome/msedge)." >&2
  exit 3
fi

# Reject host Chrome/Edge / Claude Electron profiles.
norm="$(printf '%s' "$PROFILE" | tr '[:upper:]' '[:lower:]' | tr '\\\\' '/')"
case "$norm" in
  *"/google/chrome"*|*"/microsoft/edge"*|"/.config/google-chrome"*|*"/appdata/local/google/chrome"*|*"/appdata/roaming/claude"*|*"/appdata/local/claude"*|*".config/chromium"*)
    echo "error: refusing host Chrome/Edge or Claude Electron userData as companion profile: $PROFILE" >&2
    echo "  Refusing to open the host default browser (xdg-open/chrome/msedge)." >&2
    exit 3
    ;;
esac

mkdir -p "$PROFILE"

# Unique gateway proxy: prefer already-exported HTTP_PROXY (session.env), else derive.
if [[ -z "${HTTP_PROXY:-}${HTTPS_PROXY:-}${ALL_PROXY:-}" && -n "${CLAUDE_BOX_GATEWAY_HOST:-}" ]]; then
  port="${CLAUDE_BOX_GATEWAY_HTTP_PORT:-3080}"
  export HTTP_PROXY="http://${CLAUDE_BOX_GATEWAY_HOST}:${port}"
  export HTTPS_PROXY="$HTTP_PROXY"
  export ALL_PROXY="$HTTP_PROXY"
  export NO_PROXY="${NO_PROXY:-localhost,127.0.0.1}"
fi
if [[ -z "${HTTP_PROXY:-}${ALL_PROXY:-}" ]]; then
  echo "error: unique gateway proxy is not configured (HTTP_PROXY/ALL_PROXY)." >&2
  echo "  Refusing to open a bypass path or the host default browser (xdg-open/chrome/msedge)." >&2
  exit 2
fi

args=(--user-data-dir="$PROFILE" --no-first-run --no-default-browser-check)
if [[ -n "${HTTP_PROXY:-}" ]]; then
  args+=(--proxy-server="$HTTP_PROXY")
fi
lang="${CLAUDE_BOX_ELECTRON_LANG:-}"
if [[ -z "$lang" && -n "${LANG:-}" ]]; then
  lang="${LANG%%.*}"
  lang="${lang//_/-}"
fi
if [[ -n "$lang" ]]; then
  args+=(--lang="$lang" --accept-lang="$lang")
fi

echo "[claude-box] companion-browser: $ENGINE ${args[*]} $URL"
exec "$ENGINE" "${args[@]}" "$URL"
