#!/usr/bin/env bash
# Print how to point this distro at the host Claude Box gateway / Clash.
set -euo pipefail

HOST_IP="$(ip route show default 2>/dev/null | awk '{print $3; exit}')"
HOST_IP="${HOST_IP:-<host-ip>}"
PORT="${CLAUDE_BOX_GATEWAY_PORT:-3080}"

cat << MSG
[claude-box] gateway tips (WSL → host)

1. On Windows, start ClaudeBox.Gateway / gost listening on 0.0.0.0:${PORT}
   (or the port in wsl-box.yaml), upstream → Clash HTTP/SOCKS.

2. Inside WSL, default route host is usually: ${HOST_IP}
   export HTTP_PROXY=http://${HOST_IP}:${PORT}
   export HTTPS_PROXY=http://${HOST_IP}:${PORT}
   export ALL_PROXY=http://${HOST_IP}:${PORT}
   export NO_PROXY=localhost,127.0.0.1

3. Minimum blocklist (SNI / host match — refine via JSONL):
   - *.ingest.us.sentry.io
   - browser-intake-*.datadoghq.*
   - path /api/event_logging on api.anthropic.com (careful: do not kill main API)

4. Hardening (optional): nftables output allow only → proxy IP:port.

5. Start Desktop via host Start-ClaudeBoxWsl.ps1 so env is injected automatically.

6. Optional (default OFF): features.kill_on_local_ip_change
   When the host local IP set changes, host watcher politely closes then force-kills
   Claude in this box (claude-switcher pattern). Does not replace the gateway.
   See docs/kill-on-local-ip-change.md and host/Watch-ClaudeBoxLocalIp.ps1.
MSG
