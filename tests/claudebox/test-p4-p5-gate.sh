#!/usr/bin/env bash
# P4+P5 gate: keep P1–P4 green, run mount-plan tests and existing wsl-box smokes.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"

bash tests/claudebox/test-p4-gate.sh

# P5 unit tests (core planner + session save refusal). Also included in the
# core ctest run inside the P4 gate; run them again here for an explicit line.
ctest --test-dir "$ROOT/src/claudebox/build" --output-on-failure -R 'mount_plan|box_config_settings'
QT_QPA_PLATFORM=offscreen ctest --test-dir "$ROOT/src/claudebox-ui/build" --output-on-failure -R settings_ui

# Existing wsl-box smokes remain relevant for automount / identity / framework.
bash tests/wsl-box/test-host-identity.sh
bash tests/wsl-box/test-linux-framework.sh

echo "ALL GREEN (P1+P2+P3+P4+P5 runtime and visible_dirs gate)"
