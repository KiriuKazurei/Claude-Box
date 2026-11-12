#!/usr/bin/env bash
# P3 wiring: hide / override / expose through apply-host-identity.sh.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"

fail() { echo "FAIL: $*" >&2; exit 1; }
pass() { echo "OK: $*"; }

FIXTURE="$ROOT/tests/wsl-box/fixtures/host-inventory.raw.yaml"
APPLY="$ROOT/src/wsl-box/guest/apply-host-identity.sh"
VERIFY="$ROOT/src/wsl-box/guest/verify-host-identity.sh"
[[ -f "$FIXTURE" ]] || fail "missing fixture $FIXTURE"
[[ -x "$APPLY" && -x "$VERIFY" ]] || fail "missing apply/verify scripts"

BUILD="$ROOT/src/claudebox/build"
cmake -S "$ROOT/src/claudebox" -B "$BUILD" >/dev/null
cmake --build "$BUILD" --target claudebox-identity

export CLAUDE_BOX_PREFIX="${CLAUDE_BOX_PREFIX:-$(mktemp -d /tmp/claudebox-p3-wiring.XXXXXX)}"
export CLAUDE_BOX_HOST_INVENTORY="$FIXTURE"
export CLAUDE_BOX_HIDE_IDENTIFIERS=true
export CLAUDE_BOX_IDENTITY_BIN="$BUILD/claudebox-identity"
export CLAUDE_BOX_REPO="$ROOT"
unset CLAUDE_BOX_IDENTITY_POLICY || true
mkdir -p "$CLAUDE_BOX_PREFIX"

cleanup() {
  if [[ "${CLAUDE_BOX_KEEP_PREFIX:-}" != "1" ]]; then
    rm -rf "$CLAUDE_BOX_PREFIX"
  fi
}
trap cleanup EXIT

advertised_files() {
  local f
  for f in \
    "$CLAUDE_BOX_PREFIX/env/host-model.env" \
    "$CLAUDE_BOX_PREFIX/env/session.env" \
    "$CLAUDE_BOX_PREFIX/env/spoof.env" \
    "$CLAUDE_BOX_PREFIX/var/host-identity.allowed.yaml" \
    "$CLAUDE_BOX_PREFIX/var/host-identity.presented.yaml"
  do
    [[ -f "$f" ]] && printf '%s\n' "$f"
  done
  if [[ -d "$CLAUDE_BOX_PREFIX/presentation" ]]; then
    find "$CLAUDE_BOX_PREFIX/presentation" -type f
  fi
}

assert_not_in_presentation() {
  local val="$1"
  local f
  while IFS= read -r f; do
    [[ -n "$f" ]] || continue
    if grep -F -q -- "$val" "$f"; then
      fail "value $val leaked into presentation file $f"
    fi
  done < <(advertised_files)
}

# --- default apply first, so later hide/override must clear stale files ---
bash "$APPLY"
bash "$VERIFY" || fail "verify failed on default policy"
[[ "$(tr -d '\n' < "$CLAUDE_BOX_PREFIX/presentation/dmi/id/sys_vendor")" == "Contoso Computers" ]] \
  || fail "default apply did not publish manufacturer"
[[ "$(tr -d '\n' < "$CLAUDE_BOX_PREFIX/presentation/dmi/id/product_name")" == "ContosoBook 15 G3" ]] \
  || fail "default apply did not publish product"
assert_not_in_presentation 'SN-C0NT0S0-9F3A-0001'
pass "default policy publishes model and hides identifiers"

# --- hide: hide a field that defaults to shown ---
POLICY_HIDE="$CLAUDE_BOX_PREFIX/var/policy-hide.yaml"
cat > "$POLICY_HIDE" <<'YAML'
fields:
  manufacturer:
    mode: hide
YAML
export CLAUDE_BOX_IDENTITY_POLICY="$POLICY_HIDE"
bash "$APPLY"
bash "$VERIFY" || fail "verify failed after hiding manufacturer"
if [[ -e "$CLAUDE_BOX_PREFIX/presentation/dmi/id/sys_vendor" ]]; then
  fail "sys_vendor still present after manufacturer=hide"
fi
[[ "$(tr -d '\n' < "$CLAUDE_BOX_PREFIX/presentation/dmi/id/product_name")" == "ContosoBook 15 G3" ]] \
  || fail "product disappeared after hiding manufacturer"
assert_not_in_presentation 'Contoso Computers'
assert_not_in_presentation 'SN-C0NT0S0-9F3A-0001'
grep -q 'ContosoBook 15 G3' "$CLAUDE_BOX_PREFIX/var/host-identity.presented.yaml" \
  || fail "presented.yaml missing product after hide"
grep -q 'ContosoBook 15 G3' "$CLAUDE_BOX_PREFIX/env/host-model.env" \
  || fail "host-model.env missing product after hide"
if grep -q 'CLAUDE_BOX_HOST_MANUFACTURER=' "$CLAUDE_BOX_PREFIX/env/host-model.env"; then
  fail "host-model.env still exports manufacturer after hide"
fi
pass "hide: manufacturer removed from presentation; product still shown"

# --- override: modified value is shown; original must not leak ---
POLICY_OVERRIDE="$CLAUDE_BOX_PREFIX/var/policy-override.yaml"
cat > "$POLICY_OVERRIDE" <<'YAML'
fields:
  product:
    mode: override
    value: "AdventureBook 14"
YAML
export CLAUDE_BOX_IDENTITY_POLICY="$POLICY_OVERRIDE"
bash "$APPLY"
bash "$VERIFY" || fail "verify failed after product override"
[[ "$(tr -d '\n' < "$CLAUDE_BOX_PREFIX/presentation/dmi/id/sys_vendor")" == "Contoso Computers" ]] \
  || fail "manufacturer missing after product override"
[[ "$(tr -d '\n' < "$CLAUDE_BOX_PREFIX/presentation/dmi/id/product_name")" == "AdventureBook 14" ]] \
  || fail "product_name was not overridden"
assert_not_in_presentation 'ContosoBook 15 G3'
assert_not_in_presentation 'SN-C0NT0S0-9F3A-0001'
grep -q 'AdventureBook 14' "$CLAUDE_BOX_PREFIX/env/host-model.env" \
  || fail "host-model.env missing override product"
grep -q 'AdventureBook 14' "$CLAUDE_BOX_PREFIX/var/host-identity.presented.yaml" \
  || fail "presented.yaml missing override product"
pass "override: modified product is shown; original model does not leak"

# --- expose / choose-to-show: only the opted-in identifier appears ---
POLICY_EXPOSE="$CLAUDE_BOX_PREFIX/var/policy-expose.yaml"
cat > "$POLICY_EXPOSE" <<'YAML'
fields:
  serial_number:
    mode: expose
YAML
export CLAUDE_BOX_IDENTITY_POLICY="$POLICY_EXPOSE"
bash "$APPLY"
bash "$VERIFY" || fail "verify failed after exposing serial_number"
[[ -f "$CLAUDE_BOX_PREFIX/presentation/dmi/id/product_serial" ]] \
  || fail "product_serial missing after exposing serial_number"
[[ "$(tr -d '\n' < "$CLAUDE_BOX_PREFIX/presentation/dmi/id/product_serial")" == "SN-C0NT0S0-9F3A-0001" ]] \
  || fail "product_serial value mismatch"
grep -q 'SN-C0NT0S0-9F3A-0001' "$CLAUDE_BOX_PREFIX/var/host-identity.presented.yaml" \
  || fail "presented.yaml missing exposed serial"
grep -q 'SN-C0NT0S0-9F3A-0001' "$CLAUDE_BOX_PREFIX/env/host-model.env" \
  || fail "host-model.env missing exposed serial"
# Other identifiers stay hidden unless also opted in.
other_ids=(
  '550e8400-e29b-41d4-a716-446655440000'
  '6ba7b810-9dad-11d1-80b4-00c04fd430c8'
  '7c9e6679-7425-40de-944b-e07fc1f90ae7'
  '02:11:22:33:44:55'
  'WD-WCC4N7FIXTURE'
  'BFEBFBFF000906EA'
  '8d4f3c2a-1b6e-4f90-9c11-deadbeef1234'
  'VEN_8086'
)
for val in "${other_ids[@]}"; do
  assert_not_in_presentation "$val"
done
for bad in product_uuid board_uuid chassis_uuid board_serial chassis_serial; do
  [[ -e "$CLAUDE_BOX_PREFIX/presentation/dmi/id/$bad" ]] && fail "unexpected DMI file $bad after exposing only serial"
done
# Model fields remain visible under defaults.
[[ "$(tr -d '\n' < "$CLAUDE_BOX_PREFIX/presentation/dmi/id/sys_vendor")" == "Contoso Computers" ]] \
  || fail "manufacturer missing after exposing serial"
[[ "$(tr -d '\n' < "$CLAUDE_BOX_PREFIX/presentation/dmi/id/product_name")" == "ContosoBook 15 G3" ]] \
  || fail "product missing after exposing serial"
pass "expose: only opted-in serial is presented; other identifiers stay hidden"

# Revert to default policy: the previously exposed serial must leave the layer.
unset CLAUDE_BOX_IDENTITY_POLICY
rm -f "$CLAUDE_BOX_PREFIX/var/host-identity.policy.yaml"
bash "$APPLY"
bash "$VERIFY" || fail "verify failed after reverting to defaults"
[[ -e "$CLAUDE_BOX_PREFIX/presentation/dmi/id/product_serial" ]] \
  && fail "product_serial left behind after reverting to default policy"
assert_not_in_presentation 'SN-C0NT0S0-9F3A-0001'
pass "expose cleanup: serial removed when policy returns to defaults"

echo "ALL GREEN (P3 presentation wiring)"
