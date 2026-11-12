#!/usr/bin/env bash
# Combined gate: keep P1–P9 green, plus Desktop start/stop dry-run control (P10).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"

bash tests/claudebox/test-p9-gate.sh

cmake -S "$ROOT/src/claudebox-ui" -B "$ROOT/src/claudebox-ui/build"
cmake --build "$ROOT/src/claudebox-ui/build"
cmake -S "$ROOT/src/claudebox" -B "$ROOT/src/claudebox/build"
cmake --build "$ROOT/src/claudebox/build"

ctest --test-dir "$ROOT/src/claudebox/build" --output-on-failure \
  -R 'desktop_control|identity_vault|companion_browser'
QT_QPA_PLATFORM=offscreen ctest --test-dir "$ROOT/src/claudebox-ui/build" \
  --output-on-failure -R settings_ui

echo "ALL GREEN (P1+P2+P3+P4+P5+P6+P7+IP-change+P8+P9+P10 Desktop start/stop dry-run: missing binary is not started)"
