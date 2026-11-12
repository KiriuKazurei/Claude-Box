#!/usr/bin/env bash
# Master near-beta dry-run gate: everything that can be proven on this cloud
# computer (P1–P10). Does not start W1, enable WSL, or launch a live Desktop.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"

bash tests/claudebox/test-p10-gate.sh

# One more explicit full-suite pass after the nested P10 gate, so the
# master line is only printed when every dry-run test is green.
cmake -S "$ROOT/src/claudebox-ui" -B "$ROOT/src/claudebox-ui/build"
cmake --build "$ROOT/src/claudebox-ui/build"
cmake -S "$ROOT/src/claudebox" -B "$ROOT/src/claudebox/build"
cmake --build "$ROOT/src/claudebox/build"

ctest --test-dir "$ROOT/src/claudebox/build" --output-on-failure
QT_QPA_PLATFORM=offscreen ctest --test-dir "$ROOT/src/claudebox-ui/build" \
  --output-on-failure

echo "ALL GREEN (P11 near-beta dry-run integration: P1–P10 proven on this cloud computer; real-machine W1 not started)"
