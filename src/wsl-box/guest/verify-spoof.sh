#!/usr/bin/env bash
# Probe timezone/locale as Electron/Intl would see them (node if present, else date),
# then verify host model presentation / identifier hiding.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

echo "[claude-box] verify-spoof"
echo "env TZ=${TZ:-<(unset)>} LANG=${LANG:-<(unset)>} LC_ALL=${LC_ALL:-<(unset)>}"
echo -n "/etc/localtime -> "; readlink -f /etc/localtime 2>/dev/null || ls -l /etc/localtime 2>/dev/null || true
echo -n "date: "; date
echo -n "/mnt/c present? "; if [[ -e /mnt/c ]]; then echo "YES (automount may still be on)"; else echo "no (good if automount disabled / cloud Linux)"; fi
echo -n "visible mounts: "; ls -la /mnt/claude-box 2>/dev/null || echo "(none yet)"
if [[ -n "${CLAUDE_BOX_PREFIX:-}" ]]; then
  echo "CLAUDE_BOX_PREFIX=$CLAUDE_BOX_PREFIX"
fi

if command -v node >/dev/null 2>&1; then
  node -e 'const o=Intl.DateTimeFormat().resolvedOptions(); console.log("Intl timeZone=", o.timeZone, "locale=", o.locale)'
else
  echo "node not installed — Intl probe skipped (install nodejs for Electron-parity check)"
fi

echo "Telemetry block reminder: ensure gateway blocks Sentry / Datadog RUM / event_logging"

# Host identity contract (presentation layer, not live /sys/class/dmi).
if [[ -x "$SCRIPT_DIR/verify-host-identity.sh" ]]; then
  bash "$SCRIPT_DIR/verify-host-identity.sh"
fi
