#!/usr/bin/env bash
# Full P1+P2+P3 gate: engine, collector, presentation wiring, and existing smoke.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"

bash tests/claudebox/test-p1-p2-gate.sh
bash tests/wsl-box/test-host-identity.sh
bash tests/wsl-box/test-linux-framework.sh
bash tests/claudebox/test-p3-wiring.sh

echo "ALL GREEN (P1+P2+P3 identity presentation wiring gate)"
