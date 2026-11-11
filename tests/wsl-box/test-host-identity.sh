#!/usr/bin/env bash
# Host identity contract: real-looking manufacturer/product may be published;
# hardware identifiers from a simulated host inventory must not leak;
# TZ/locale spoofing still applies. Uses fictional fixture IDs only.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"

fail() { echo "FAIL: $*" >&2; exit 1; }
pass() { echo "OK: $*"; }

FIXTURE="$ROOT/tests/wsl-box/fixtures/host-inventory.raw.yaml"
[[ -f "$FIXTURE" ]] || fail "missing fixture $FIXTURE"

# Fixture must contain a model AND at least one forbidden identifier (contract of this test).
grep -q 'Contoso Computers' "$FIXTURE" || fail "fixture missing manufacturer"
grep -q 'ContosoBook 15 G3' "$FIXTURE" || fail "fixture missing product"
grep -q 'SN-C0NT0S0-9F3A-0001' "$FIXTURE" || fail "fixture missing serial (needed to prove hiding)"
grep -q '8d4f3c2a-1b6e-4f90-9c11-deadbeef1234' "$FIXTURE" || fail "fixture missing machine_guid"
pass "fixture has model + forbidden identifiers"

# Example config encodes the hard policy
cfg=src/wsl-box/config/wsl-box.example.yaml
grep -q 'hide_identifiers:\s*true' "$cfg" || fail "example yaml must set hide_identifiers: true"
grep -q 'expose_real_model:\s*true' "$cfg" || fail "example yaml must set expose_real_model: true"
for key in serial chassis_uuid mac disk_serial cpu_id machine_guid device_instance_path; do
  grep -qE "^[[:space:]]*-[[:space:]]*${key}[[:space:]]*$" "$cfg" \
    || fail "example yaml hidden_identifier_keys missing $key"
done
pass "example yaml host_identity policy"

export CLAUDE_BOX_PREFIX="${ROOT}/.run/claude-box-identity-$$"
export CLAUDE_BOX_HOST_INVENTORY="$FIXTURE"
trap 'rm -rf "$CLAUDE_BOX_PREFIX"' EXIT

src/wsl-box/linux/Install-ClaudeBoxFramework.sh
# Source session env (TZ + hide_identifiers) then apply
# shellcheck disable=SC1091
source "$CLAUDE_BOX_PREFIX/env/session.env"
export CLAUDE_BOX_HOST_INVENTORY="$FIXTURE"
src/wsl-box/linux/Run-Smoke.sh

# Model visible in the presentation layer
man="$(tr -d '\n' < "$CLAUDE_BOX_PREFIX/presentation/dmi/id/sys_vendor")"
prod="$(tr -d '\n' < "$CLAUDE_BOX_PREFIX/presentation/dmi/id/product_name")"
[[ "$man" == "Contoso Computers" ]] || fail "sys_vendor='$man'"
[[ "$prod" == "ContosoBook 15 G3" ]] || fail "product_name='$prod'"
grep -q 'Contoso Computers' "$CLAUDE_BOX_PREFIX/env/host-model.env" || fail "host-model.env missing manufacturer"
grep -q 'ContosoBook 15 G3' "$CLAUDE_BOX_PREFIX/env/host-model.env" || fail "host-model.env missing product"
pass "manufacturer and product published"

# Identifiers must not appear in advertised paths
forbidden_vals=(
  'SN-C0NT0S0-9F3A-0001'
  '550e8400-e29b-41d4-a716-446655440000'
  '6ba7b810-9dad-11d1-80b4-00c04fd430c8'
  '7c9e6679-7425-40de-944b-e07fc1f90ae7'
  '02:11:22:33:44:55'
  'WD-WCC4N7FIXTURE'
  'BFEBFBFF000906EA'
  '8d4f3c2a-1b6e-4f90-9c11-deadbeef1234'
  'VEN_8086'
)
advertised=(
  "$CLAUDE_BOX_PREFIX/env/host-model.env"
  "$CLAUDE_BOX_PREFIX/env/session.env"
  "$CLAUDE_BOX_PREFIX/env/spoof.env"
  "$CLAUDE_BOX_PREFIX/var/host-identity.allowed.yaml"
)
while IFS= read -r -d '' f; do
  advertised+=("$f")
done < <(find "$CLAUDE_BOX_PREFIX/presentation" -type f -print0)

for val in "${forbidden_vals[@]}"; do
  for f in "${advertised[@]}"; do
    [[ -f "$f" ]] || continue
    if grep -F -q -- "$val" "$f"; then
      fail "identifier $val leaked into $f"
    fi
  done
done
for bad in product_serial product_uuid board_serial chassis_serial chassis_uuid board_asset_tag; do
  [[ -e "$CLAUDE_BOX_PREFIX/presentation/dmi/id/$bad" ]] && fail "forbidden DMI file $bad exists"
done
pass "forbidden identifiers not in presentation or advertised env"

# TZ/locale still spoofed (example config: America/Los_Angeles / en_US.UTF-8)
# shellcheck disable=SC1091
source "$CLAUDE_BOX_PREFIX/env/spoof.env"
[[ "$TZ" == "America/Los_Angeles" ]] || fail "TZ=$TZ after apply"
[[ "$LANG" == "en_US.UTF-8" ]] || fail "LANG=$LANG after apply"
if command -v node >/dev/null 2>&1; then
  node -e '
const o = Intl.DateTimeFormat().resolvedOptions();
if (o.timeZone !== "America/Los_Angeles") {
  console.error("Intl timeZone", o.timeZone);
  process.exit(1);
}
'
fi
pass "TZ/locale still spoofed after model injection"

# verify must fail if a serial is planted in the advertised layer
echo 'SN-C0NT0S0-9F3A-0001' > "$CLAUDE_BOX_PREFIX/presentation/dmi/id/product_serial"
if bash "$CLAUDE_BOX_PREFIX/guest/verify-host-identity.sh"; then
  fail "verify-host-identity must fail when a serial file is planted"
else
  pass "verify-host-identity fails on planted serial"
fi
rm -f "$CLAUDE_BOX_PREFIX/presentation/dmi/id/product_serial"
bash "$CLAUDE_BOX_PREFIX/guest/verify-host-identity.sh" || fail "verify should pass after removing planted serial"

# hide_identifiers=false must be refused
if CLAUDE_BOX_HIDE_IDENTIFIERS=false bash "$CLAUDE_BOX_PREFIX/guest/apply-host-identity.sh"; then
  fail "apply-host-identity must refuse hide_identifiers=false"
else
  pass "apply-host-identity refuses hide_identifiers=false"
fi
if CLAUDE_BOX_HIDE_IDENTIFIERS=false bash "$CLAUDE_BOX_PREFIX/guest/verify-host-identity.sh"; then
  fail "verify-host-identity must refuse hide_identifiers=false"
else
  pass "verify-host-identity refuses hide_identifiers=false"
fi

# Config-only path (no raw inventory): manufacturer/product from env, no serials
unset CLAUDE_BOX_HOST_INVENTORY
rm -f "$CLAUDE_BOX_PREFIX/var/host-inventory.raw.yaml"
export CLAUDE_BOX_HOST_MANUFACTURER='Fabrikam LLC'
export CLAUDE_BOX_HOST_PRODUCT='Fabrikam Note 14'
export CLAUDE_BOX_HIDE_IDENTIFIERS=true
bash "$CLAUDE_BOX_PREFIX/guest/apply-host-identity.sh"
man="$(tr -d '\n' < "$CLAUDE_BOX_PREFIX/presentation/dmi/id/sys_vendor")"
prod="$(tr -d '\n' < "$CLAUDE_BOX_PREFIX/presentation/dmi/id/product_name")"
[[ "$man" == "Fabrikam LLC" ]] || fail "config-only sys_vendor='$man'"
[[ "$prod" == "Fabrikam Note 14" ]] || fail "config-only product_name='$prod'"
bash "$CLAUDE_BOX_PREFIX/guest/verify-host-identity.sh" || fail "verify failed on config-only model"
pass "config/env-only model path"

# PowerShell sanitizer (optional if pwsh is present)
if command -v pwsh >/dev/null 2>&1; then
  out="$CLAUDE_BOX_PREFIX/var/from-ps1.yaml"
  pwsh -NoProfile -File src/wsl-box/host/Export-ClaudeBoxHostIdentity.ps1 \
    -FixturePath "$FIXTURE" -OutputPath "$out"
  grep -q 'Contoso Computers' "$out" || fail "ps1 output missing manufacturer"
  grep -q 'ContosoBook 15 G3' "$out" || fail "ps1 output missing product"
  if grep -E -q 'SN-C0NT0S0|550e8400|02:11:22:33:44:55|WCC4N7FIXTURE|BFEBFBFF|deadbeef1234|VEN_8086' "$out"; then
    fail "ps1 output leaked an identifier"
  fi
  pass "Export-ClaudeBoxHostIdentity.ps1 sanitizes fixtures"
fi

echo "ALL GREEN (host identity)"
