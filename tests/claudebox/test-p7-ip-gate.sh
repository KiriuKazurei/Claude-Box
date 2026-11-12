#!/usr/bin/env bash
# Combined gate: keep P1–P7 green, plus unique-gateway IP-change response
# (parallel gateway disconnect + polite process close).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"

bash tests/claudebox/test-p7-gate.sh

cmake -S "$ROOT/src/claudebox-ui" -B "$ROOT/src/claudebox-ui/build"
cmake --build "$ROOT/src/claudebox-ui/build"
cmake -S "$ROOT/src/claudebox" -B "$ROOT/src/claudebox/build"
cmake --build "$ROOT/src/claudebox/build"

ctest --test-dir "$ROOT/src/claudebox/build" --output-on-failure \
  -R 'gateway_ip_change|gateway_plan|box_config_settings|spoof_plan'
QT_QPA_PLATFORM=offscreen ctest --test-dir "$ROOT/src/claudebox-ui/build" \
  --output-on-failure -R settings_ui

bash tests/claudebox/test-watch-local-ip.sh

echo "ALL GREEN (P1+P2+P3+P4+P5+P6+P7 and IP-change gateway response: parallel disconnect + polite close)"
