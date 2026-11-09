#!/usr/bin/env bash
# Install Linux Claude Desktop (.deb). Prefer local deb via CLAUDE_BOX_DEB.
# Cloud/linux framework: does not auto-download; set CLAUDE_BOX_DEB or place under installers/linux/.
set -euo pipefail

echo "[claude-box] install-claude-desktop"
if [[ "$(id -u)" -ne 0 ]]; then
  echo "re-run as root (sudo) or via host: Invoke-ClaudeBoxGuest with root"
  exec sudo -E "$0" "$@"
fi

export DEBIAN_FRONTEND=noninteractive
apt-get update -y
apt-get install -y ca-certificates curl wget libgtk-3-0 libnotify4 libnss3 libxss1 libxtst6 xdg-utils libatspi2.0-0 libsecret-1-0 || true

DEB="${CLAUDE_BOX_DEB:-}"
URL="${CLAUDE_BOX_DEB_URL:-}"

tmpdir="$(mktemp -d)"
trap 'rm -rf "$tmpdir"' EXIT

if [[ -n "$DEB" && -f "$DEB" ]]; then
  cp -f "$DEB" "$tmpdir/claude.deb"
elif [[ -n "$URL" ]]; then
  echo "Downloading $URL"
  curl -fsSL -o "$tmpdir/claude.deb" "$URL"
else
  cat << 'MSG'
No deb provided.

Set one of:
  CLAUDE_BOX_DEB=/path/to/claude-desktop.deb
  CLAUDE_BOX_DEB_URL=https://.../claude-desktop.deb

Cloud Linux framework (preferred): place the .deb under installers/linux/
(gitignored) and run:
  src/wsl-box/linux/Install-ClaudeDesktop.sh
(or --check to only verify the file exists; that path never auto-downloads).

Official Linux Desktop packages are distributed by Anthropic; place the .deb
on the host and pass it in, or download inside WSL after authenticating.

This script will then: apt install -y ./claude.deb
MSG
  exit 2
fi

apt-get install -y "$tmpdir/claude.deb" || dpkg -i "$tmpdir/claude.deb" || apt-get install -f -y
echo "[claude-box] install done. Try: command -v claude-desktop || ls /usr/bin/*claude*"
