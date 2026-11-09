#!/usr/bin/env bash
# Print how to point this distro / cloud box at the host Claude Box gateway / Clash.
# Honors CLAUDE_BOX_GATEWAY_HOST/PORT and CLAUDE_BOX_BLOCKLIST (comma-separated).
set -euo pipefail

HOST_IP="${CLAUDE_BOX_GATEWAY_HOST:-}"
if [[ -z "$HOST_IP" || "$HOST_IP" == "auto" ]]; then
  HOST_IP="$(ip route show default 2>/dev/null | awk '{print $3; exit}')"
  HOST_IP="${HOST_IP:-127.0.0.1}"
fi
PORT="${CLAUDE_BOX_GATEWAY_PORT:-3080}"

BLOCKLIST_RAW="${CLAUDE_BOX_BLOCKLIST:-}"
if [[ -z "$BLOCKLIST_RAW" ]]; then
  BLOCKLIST_LINES="   - *.ingest.us.sentry.io
   - browser-intake-*.datadoghq.*
   - path /api/event_logging on api.anthropic.com (careful: do not kill main API)"
else
  BLOCKLIST_LINES="$(printf '%s\n' "${BLOCKLIST_RAW//,/ }" | tr ' ' '\n' | sed '/^$/d' | sed 's/^/   - /')"
fi

cat << MSG
[claude-box] gateway tips (WSL / cloud → gateway host)

1. On Windows, start ClaudeBox.Gateway / gost listening on 0.0.0.0:${PORT}
   (or the port in wsl-box.yaml), upstream → Clash HTTP/SOCKS.
   On cloud Linux dry-run, gateway may be absent — env injection is still exercised.

2. Inside the box, gateway host: ${HOST_IP}
   export HTTP_PROXY=http://${HOST_IP}:${PORT}
   export HTTPS_PROXY=http://${HOST_IP}:${PORT}
   export ALL_PROXY=http://${HOST_IP}:${PORT}
   export NO_PROXY=localhost,127.0.0.1

   Current session (if installed via linux/ framework):
   HTTP_PROXY=${HTTP_PROXY:-<(unset)>}
   HTTPS_PROXY=${HTTPS_PROXY:-<(unset)>}
   ALL_PROXY=${ALL_PROXY:-<(unset)>}

3. Blocklist (from config CLAUDE_BOX_BLOCKLIST or defaults):
${BLOCKLIST_LINES}
   Also refine via gateway JSONL after a soak run.

4. Hardening (optional): nftables output allow only → proxy IP:port.

5. Start Desktop via host Start-ClaudeBoxWsl.ps1 (Win11) so env is injected automatically.
   Cloud dry-run: src/wsl-box/linux/Run-Smoke.sh (no Desktop required).

6. Optional (default OFF): features.kill_on_local_ip_change
   When the host local IP set changes, host watcher politely closes then force-kills
   Claude in this box (claude-switcher pattern). Does not replace the gateway.
   See docs/kill-on-local-ip-change.md and host/Watch-ClaudeBoxLocalIp.ps1.
   Current config value: ${CLAUDE_BOX_KILL_ON_LOCAL_IP_CHANGE:-false}
MSG
