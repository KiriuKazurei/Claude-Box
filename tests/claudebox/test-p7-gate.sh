#!/usr/bin/env bash
# P7 gate: keep P1–P6 green, apply/verify box timezone and locale spoof,
# and keep the existing apply-spoof / verify-spoof linux framework smoke.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"

bash tests/claudebox/test-p6-gate.sh

# Rebuild so this script can be invoked alone after editing spoof sources.
cmake -S "$ROOT/src/claudebox-ui" -B "$ROOT/src/claudebox-ui/build"
cmake --build "$ROOT/src/claudebox-ui/build"
# The standalone core tree is configured by the P1 gate inside P6; refresh it
# as well so spoof_plan is present even if this script is re-run in isolation
# after a core-only edit.
cmake -S "$ROOT/src/claudebox" -B "$ROOT/src/claudebox/build"
cmake --build "$ROOT/src/claudebox/build"

ctest --test-dir "$ROOT/src/claudebox/build" --output-on-failure \
  -R 'spoof_plan|box_config_settings|gateway_plan'
QT_QPA_PLATFORM=offscreen ctest --test-dir "$ROOT/src/claudebox-ui/build" \
  --output-on-failure -R settings_ui

# Existing apply-spoof / verify-spoof contract must stay green.
# (Already run via the P5 path inside P6; run once more for an explicit line.)
bash tests/wsl-box/test-linux-framework.sh

echo "ALL GREEN (P1+P2+P3+P4+P5+P6+P7 timezone and locale spoof gate)"
