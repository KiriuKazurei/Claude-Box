#!/usr/bin/env bash
# P4 gate: keep P1+P2+P3 green, build the Qt settings page, run headless tests + smoke.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"

bash tests/claudebox/test-p1-p2-p3-gate.sh

UI_BUILD="$ROOT/src/claudebox-ui/build"
cmake -S "$ROOT/src/claudebox-ui" -B "$UI_BUILD"
cmake --build "$UI_BUILD"
QT_QPA_PLATFORM=offscreen ctest --test-dir "$UI_BUILD" --output-on-failure -R settings_ui

# Core box-config / session tests also live in the identity build (added in P4).
ctest --test-dir "$ROOT/src/claudebox/build" --output-on-failure -R box_config_settings

PREFIX="$(mktemp -d /tmp/claudebox-p4-gate.XXXXXX)"
mkdir -p "$PREFIX/config" "$PREFIX/var"
cp "$ROOT/src/wsl-box/config/wsl-box.example.yaml" "$PREFIX/config/wsl-box.yaml"
cp "$ROOT/tests/wsl-box/fixtures/host-inventory.raw.yaml" "$PREFIX/var/host-inventory.raw.yaml"
QT_QPA_PLATFORM=offscreen "$UI_BUILD/claudebox-settings" \
  --prefix "$PREFIX" \
  --config "$PREFIX/config/wsl-box.yaml" \
  --inventory "$PREFIX/var/host-inventory.raw.yaml" \
  --smoke

echo "ALL GREEN (P1+P2+P3+P4 settings shell gate)"
