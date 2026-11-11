#!/usr/bin/env bash
# P1 gate: build the identity engine and run its tests on the cloud box.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
BUILD="$ROOT/src/claudebox/build"
cmake -S "$ROOT/src/claudebox" -B "$BUILD"
cmake --build "$BUILD"
ctest --test-dir "$BUILD" --output-on-failure
PREFIX="$(mktemp -d /tmp/claudebox-p1-gate.XXXXXX)"
"$BUILD/claudebox-identity" apply \
  --inventory "$ROOT/tests/wsl-box/fixtures/host-inventory.raw.yaml" \
  --prefix "$PREFIX"
"$BUILD/claudebox-identity" verify \
  --inventory "$ROOT/tests/wsl-box/fixtures/host-inventory.raw.yaml" \
  --prefix "$PREFIX"
# Hidden identifiers must not appear in advertised files.
if grep -R -F "SN-C0NT0S0-9F3A-0001" "$PREFIX/env" "$PREFIX/presentation" "$PREFIX/var/host-identity.presented.yaml" "$PREFIX/var/host-identity.allowed.yaml" >/dev/null 2>&1; then
  echo "FAIL: serial leaked into presentation layer"
  exit 1
fi
echo "ALL GREEN (P1 identity engine gate)"
