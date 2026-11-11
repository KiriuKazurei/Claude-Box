#!/usr/bin/env bash
# Full P1+P2 gate: identity engine, collector, and collect -> apply -> verify.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
BUILD="$ROOT/src/claudebox/build"
SYS_FIX="$ROOT/tests/claudebox/fixtures/host-sys"
cmake -S "$ROOT/src/claudebox" -B "$BUILD"
cmake --build "$BUILD"
ctest --test-dir "$BUILD" --output-on-failure

PREFIX="$(mktemp -d /tmp/claudebox-p1-p2-gate.XXXXXX)"
"$BUILD/claudebox-identity" collect --source-root "$SYS_FIX" --prefix "$PREFIX"
RAW="$PREFIX/var/host-inventory.raw.yaml"
test -f "$RAW"
grep -q "SN-C0NT0S0-9F3A-0001" "$RAW"
# Collect alone must not publish a presentation layer.
if [[ -e "$PREFIX/presentation" || -e "$PREFIX/env/host-model.env" ]]; then
  echo "FAIL: collect wrote a presentation layer"
  exit 1
fi

"$BUILD/claudebox-identity" apply --inventory "$RAW" --prefix "$PREFIX"
"$BUILD/claudebox-identity" verify --inventory "$RAW" --prefix "$PREFIX"
if grep -R -F "SN-C0NT0S0-9F3A-0001" "$PREFIX/env" "$PREFIX/presentation" "$PREFIX/var/host-identity.presented.yaml" "$PREFIX/var/host-identity.allowed.yaml" >/dev/null 2>&1; then
  echo "FAIL: serial leaked into presentation layer"
  exit 1
fi
grep -q "Contoso Computers" "$PREFIX/var/host-identity.presented.yaml"
grep -q "SN-C0NT0S0-9F3A-0001" "$RAW"
echo "ALL GREEN (P1+P2 identity collect/apply/verify gate)"
