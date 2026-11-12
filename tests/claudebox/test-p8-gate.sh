#!/usr/bin/env bash
# Combined gate: keep P1–P7 and IP-change green, plus companion fingerprint browser (P8).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"

bash tests/claudebox/test-p7-ip-gate.sh

cmake -S "$ROOT/src/claudebox-ui" -B "$ROOT/src/claudebox-ui/build"
cmake --build "$ROOT/src/claudebox-ui/build"
cmake -S "$ROOT/src/claudebox" -B "$ROOT/src/claudebox/build"
cmake --build "$ROOT/src/claudebox/build"

ctest --test-dir "$ROOT/src/claudebox/build" --output-on-failure \
  -R 'companion_browser|gateway_plan|spoof_plan|box_config_settings|gateway_ip_change'
QT_QPA_PLATFORM=offscreen ctest --test-dir "$ROOT/src/claudebox-ui/build" \
  --output-on-failure -R settings_ui

echo "ALL GREEN (P1+P2+P3+P4+P5+P6+P7+IP-change+P8 companion fingerprint browser: no host-browser fallback)"
