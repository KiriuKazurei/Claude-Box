#!/usr/bin/env bash
# P6 gate: keep P1–P5 green, run unique-gateway plan/blocklist/JSONL/status tests
# and the existing ClaudeBox.Gateway structure smoke when pwsh is available.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"

bash tests/claudebox/test-p4-p5-gate.sh

# P6 unit tests (core planner + JSONL + config round-trip). Also included in
# the core ctest run after the UI rebuild in the P4 gate; run them again here
# for an explicit line. Rebuild the core in case this script is invoked alone
# after the previous gate already compiled — cmake is cheap and keeps sources
# in sync when only this file is re-run.
cmake -S "$ROOT/src/claudebox-ui" -B "$ROOT/src/claudebox-ui/build"
cmake --build "$ROOT/src/claudebox-ui/build"
ctest --test-dir "$ROOT/src/claudebox/build" --output-on-failure -R 'gateway_plan|box_config_settings|mount_plan'
QT_QPA_PLATFORM=offscreen ctest --test-dir "$ROOT/src/claudebox-ui/build" --output-on-failure -R settings_ui

if command -v pwsh >/dev/null 2>&1; then
  pwsh -File tests/Test-Structure.ps1
else
  echo "NOTE: pwsh not found; skipped tests/Test-Structure.ps1 (ClaudeBox.Gateway smoke)."
fi

echo "ALL GREEN (P1+P2+P3+P4+P5+P6 unique gateway gate)"
