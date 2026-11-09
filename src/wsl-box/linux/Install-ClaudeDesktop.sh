#!/usr/bin/env bash
# Install Linux Claude Desktop from a *local* .deb under installers/linux/ only.
# Never auto-downloads. Cloud dry-run: use --check to verify deb presence without dpkg.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=lib/common.sh
source "$SCRIPT_DIR/lib/common.sh"

ROOT="$(claude_box_repo_root)"
PREFIX="${CLAUDE_BOX_PREFIX:-$(claude_box_default_prefix)}"
CHECK_ONLY=0
FORCE_URL=0

for arg in "$@"; do
  case "$arg" in
    --check) CHECK_ONLY=1 ;;
    --allow-url) FORCE_URL=1 ;; # escape hatch; not used by smoke
    -h|--help)
      cat << HELP
Usage: $(basename "$0") [--check] [--allow-url]

Looks for a local .deb under:
  \$CLAUDE_BOX_DEB
  ${ROOT}/installers/linux/*.deb

Without a local deb, exits 2 with instructions (no download).
--check: only verify the deb exists; do not run apt/dpkg.
HELP
      exit 0
      ;;
  esac
done

find_local_deb() {
  if [[ -n "${CLAUDE_BOX_DEB:-}" && -f "${CLAUDE_BOX_DEB}" ]]; then
    printf '%s\n' "$CLAUDE_BOX_DEB"
    return 0
  fi
  local d
  d="$(ls -1t "${ROOT}/installers/linux/"*.deb 2>/dev/null | head -1 || true)"
  if [[ -n "$d" && -f "$d" ]]; then
    printf '%s\n' "$d"
    return 0
  fi
  return 1
}

DEB=""
if DEB="$(find_local_deb)"; then
  claude_box_log "local deb: $DEB"
else
  cat << MSG >&2
[claude-box/linux] error: no local Claude Desktop .deb found.

Place a package at one of:
  ${ROOT}/installers/linux/claude-desktop_*.deb
  or set CLAUDE_BOX_DEB=/path/to/claude-desktop.deb

This framework does not auto-download Anthropic packages.
On Win11 WSL, copy the deb into the distro then run guest/install-claude-desktop.sh.
MSG
  exit 2
fi

if [[ "$CHECK_ONLY" -eq 1 ]]; then
  claude_box_log "CHECK OK (deb present; install skipped)"
  exit 0
fi

if [[ ! -d "${PREFIX}/guest" ]]; then
  "$SCRIPT_DIR/Install-ClaudeBoxFramework.sh"
fi

export CLAUDE_BOX_DEB="$DEB"
# Clear URL unless explicitly allowed — guest script must not download by default here
if [[ "$FORCE_URL" -ne 1 ]]; then
  unset CLAUDE_BOX_DEB_URL || true
  export CLAUDE_BOX_DEB_URL=""
fi

claude_box_log "invoking guest install (needs root/sudo for apt)"
exec bash "${PREFIX}/guest/install-claude-desktop.sh"
