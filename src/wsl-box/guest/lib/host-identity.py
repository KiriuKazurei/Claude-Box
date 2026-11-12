#!/usr/bin/env python3
"""Legacy host-identity helpers (kept for structure tests and reference).

P3: apply-host-identity.sh and verify-host-identity.sh generate and check
the presentation layer through the C++ claudebox-identity engine. Do not
reintroduce a parallel policy implementation here.

This module does not hide that the environment is WSL/QEMU; kernel names
such as microsoft-standard-WSL2 are out of scope and are left visible.
"""
from __future__ import annotations

import os
import re
import sys
from pathlib import Path

ALLOWED_ALIASES = {
    "manufacturer": (
        "manufacturer",
        "sys_vendor",
        "vendor",
        "system_manufacturer",
        "win32_computersystem.manufacturer",
    ),
    "product": (
        "product",
        "product_name",
        "productname",
        "model",
        "system_product_name",
        "win32_computersystem.model",
    ),
}

# Canonical hidden identifier keys (normalized: lower, non-alnum -> underscore).
HIDDEN_IDENTIFIER_KEYS = (
    "serial",
    "serial_number",
    "serialnumber",
    "product_serial",
    "board_serial",
    "chassis_serial",
    "bios_serial",
    "disk_serial",
    "volume_serial",
    "uuid",
    "product_uuid",
    "board_uuid",
    "chassis_uuid",
    "machine_uuid",
    "system_uuid",
    "mac",
    "mac_address",
    "macaddress",
    "cpu_id",
    "processor_id",
    "processorid",
    "machine_guid",
    "machineguid",
    "device_instance_path",
    "pnp_device_id",
    "pnpdeviceid",
    "instancename",
    "asset_tag",
    "board_asset_tag",
    "chassis_asset_tag",
)

# Filenames we will never write under presentation/dmi/id.
FORBIDDEN_DMI_NAMES = (
    "product_serial",
    "product_uuid",
    "board_serial",
    "board_asset_tag",
    "chassis_serial",
    "chassis_asset_tag",
    "chassis_uuid",
    "product_sku",  # often a unique encoding; do not publish
)

# Variable *assignments* that would publish an identifier (not the hide-list itself).
_FORBIDDEN_ASSIGN_RE = re.compile(
    r"(?i)^\s*(?:export\s+)?(?:CLAUDE_BOX_HOST_)?("
    + "|".join(re.escape(k) for k in HIDDEN_IDENTIFIER_KEYS)
    + r")\s*="
)

_KEY_RE = re.compile(r"(?m)^([A-Za-z0-9_.]+)\s*:\s*(.*?)\s*$")


def normalize_key(key: str) -> str:
    return re.sub(r"[^a-z0-9]+", "_", key.strip().lower()).strip("_")


def _unquote(val: str) -> str:
    val = val.strip()
    if val.startswith("#") and val in {"#",}:
        return ""
    # strip inline comments when unquoted
    if val[:1] not in {"'", '"'}:
        val = re.split(r"\s+#", val, maxsplit=1)[0].strip()
        return val
    q = val[0]
    if val.endswith(q) and len(val) >= 2:
        inner = val[1:-1]
        return bytes(inner, "utf-8").decode("unicode_escape") if "\\" in inner else inner
    return val.strip("\"'")


def parse_flat_yaml(path: str | Path) -> dict[str, str]:
    """Parse a flat (or dotted) key: value YAML file. Nested maps are flattened
    only if written as already-flat keys. Comments and empty values allowed.
    """
    text = Path(path).read_text(encoding="utf-8")
    out: dict[str, str] = {}
    for m in _KEY_RE.finditer(text):
        raw_key, raw_val = m.group(1), m.group(2)
        if raw_val.strip() in {"", "|", ">", "[]"}:
            continue
        val = _unquote(raw_val)
        if val == "" or val == "~" or val.lower() == "null":
            continue
        out[raw_key] = val
        out[normalize_key(raw_key)] = val
    return out


def classify_key(key: str) -> str:
    n = normalize_key(key)
    for field, aliases in ALLOWED_ALIASES.items():
        if n in {normalize_key(a) for a in aliases}:
            return field
    if n in HIDDEN_IDENTIFIER_KEYS:
        return "forbidden"
    # Heuristic: keys that look like identifiers
    if any(tok in n.split("_") for tok in ("serial", "uuid", "guid", "mac")):
        return "forbidden"
    if n.endswith("_id") and n in {"cpu_id", "processor_id", "pnp_device_id"}:
        return "forbidden"
    return "unknown"


def sanitize(inventory: dict[str, str]) -> dict[str, str]:
    allowed: dict[str, str] = {}
    for key, val in inventory.items():
        kind = classify_key(key)
        if kind in ALLOWED_ALIASES and val.strip():
            # First wins unless a canonical key overwrites later — prefer exact names
            if kind not in allowed or normalize_key(key) in {kind, "model" if kind == "product" else kind}:
                allowed[kind] = val.strip()
    return allowed


def forbidden_values(inventory: dict[str, str]) -> list[str]:
    vals: list[str] = []
    seen: set[str] = set()
    for key, val in inventory.items():
        if classify_key(key) != "forbidden":
            continue
        item = val.strip()
        if not item or item in seen:
            continue
        seen.add(item)
        vals.append(item)
    return vals


def parse_truthy(val: str | None, default: bool = True) -> bool:
    if val is None or val == "":
        return default
    return val.strip().lower() in {"1", "true", "yes", "on"}


def hide_identifiers_required(env: dict[str, str] | None = None) -> None:
    env = env or os.environ
    flag = env.get("CLAUDE_BOX_HIDE_IDENTIFIERS", "true")
    if not parse_truthy(flag, default=True):
        raise SystemExit(
            "error: host_identity.hide_identifiers must stay true "
            f"(got {flag!r}); refusing to publish host identity"
        )


# UUID / MAC-ish values must never appear as published model fields either.
_UUID_RE = re.compile(
    r"(?i)\b[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}\b"
)
_MAC_RE = re.compile(r"(?i)\b(?:[0-9a-f]{2}[:-]){5}[0-9a-f]{2}\b")


def looks_like_identifier(val: str) -> str | None:
    if _UUID_RE.search(val):
        return "uuid-or-guid"
    if _MAC_RE.search(val):
        return "mac-address"
    return None


def write_presentation(prefix: Path, allowed: dict[str, str]) -> None:
    env_dir = prefix / "env"
    pres_dmi = prefix / "presentation" / "dmi" / "id"
    env_dir.mkdir(parents=True, exist_ok=True)
    pres_dmi.mkdir(parents=True, exist_ok=True)

    # Never leave stale identifier files from a previous run.
    for name in FORBIDDEN_DMI_NAMES:
        p = pres_dmi / name
        if p.exists():
            p.unlink()

    manufacturer = allowed.get("manufacturer", "")
    product = allowed.get("product", "")
    for field, val in (("manufacturer", manufacturer), ("product", product)):
        kind = looks_like_identifier(val)
        if kind:
            raise SystemExit(
                f"error: host_identity.{field} looks like a {kind}, refusing to publish"
            )

    (pres_dmi / "sys_vendor").write_text(manufacturer + ("\n" if manufacturer else ""), encoding="utf-8")
    (pres_dmi / "product_name").write_text(product + ("\n" if product else ""), encoding="utf-8")

    lines = [
        "# Generated by apply-host-identity.sh — manufacturer + product only.",
        "# Do not add serials, UUIDs, MACs, or other hardware identifiers.",
        f"export CLAUDE_BOX_HOST_MANUFACTURER={_shell_quote(manufacturer)}",
        f"export CLAUDE_BOX_HOST_PRODUCT={_shell_quote(product)}",
    ]
    (env_dir / "host-model.env").write_text("\n".join(lines) + "\n", encoding="utf-8")

    allowed_yaml = prefix / "var" / "host-identity.allowed.yaml"
    allowed_yaml.parent.mkdir(parents=True, exist_ok=True)
    allowed_yaml.write_text(
        "# Sanitized host identity — manufacturer + product only.\n"
        f"manufacturer: {_yaml_quote(manufacturer)}\n"
        f"product: {_yaml_quote(product)}\n",
        encoding="utf-8",
    )

    manifest = prefix / "presentation" / "MANIFEST.txt"
    hidden = ", ".join(HIDDEN_IDENTIFIER_KEYS)
    manifest.write_text(
        "layer=presentation\n"
        "fields=sys_vendor,product_name\n"
        f"manufacturer={manufacturer}\n"
        f"product={product}\n"
        "live_sysfs=not rewritten (kernel DMI stays as-is; this tree is the contract)\n"
        f"hidden_identifier_keys={hidden}\n",
        encoding="utf-8",
    )


def _shell_quote(s: str) -> str:
    return "'" + s.replace("'", "'\"'\"'") + "'"


def _yaml_quote(s: str) -> str:
    if s == "":
        return '""'
    if any(c in s for c in ":#{}[]&*!|>'\"%@`"):
        return '"' + s.replace("\\", "\\\\").replace('"', '\\"') + '"'
    return s


def advertised_paths(prefix: Path) -> list[Path]:
    paths: list[Path] = []
    for rel in (
        "env/host-model.env",
        "env/session.env",
        "env/spoof.env",
        "presentation",
        "var/host-identity.allowed.yaml",
    ):
        p = prefix / rel
        if p.is_file():
            paths.append(p)
        elif p.is_dir():
            for f in p.rglob("*"):
                if f.is_file():
                    paths.append(f)
    return paths


def scan_leaks(prefix: Path, forbidden: list[str]) -> list[str]:
    """Return human-readable leak descriptions. Raw inventory under var/ is not advertised
    except host-identity.allowed.yaml (must stay clean).
    """
    leaks: list[str] = []
    # Identifier files in the DMI presentation tree
    dmi = prefix / "presentation" / "dmi" / "id"
    if dmi.is_dir():
        for f in dmi.iterdir():
            if f.is_file() and f.name in FORBIDDEN_DMI_NAMES:
                leaks.append(f"forbidden DMI file present: {f}")

    for path in advertised_paths(prefix):
        try:
            text = path.read_text(encoding="utf-8", errors="replace")
        except OSError:
            continue
        # env key names that advertise identifiers
        upper = text.upper()
        if path.suffix == ".env" or path.name.endswith(".env"):
            for line in text.splitlines():
                stripped = line.strip()
                if not stripped or stripped.startswith("#"):
                    continue
                # Policy lists (CLAUDE_BOX_HIDDEN_IDENTIFIER_KEYS=serial,...) are not leaks.
                if stripped.startswith("export CLAUDE_BOX_HIDDEN_IDENTIFIER_KEYS") or \
                   stripped.startswith("CLAUDE_BOX_HIDDEN_IDENTIFIER_KEYS"):
                    continue
                m = _FORBIDDEN_ASSIGN_RE.match(stripped)
                if m:
                    leaks.append(f"{path}: advertised identifier assignment {m.group(1)}")
        for val in forbidden:
            if val and val in text:
                leaks.append(f"{path}: leaked identifier value {val!r}")
    return leaks


def _env_model() -> dict[str, str]:
    inv: dict[str, str] = {}
    man = os.environ.get("CLAUDE_BOX_HOST_MANUFACTURER", "").strip()
    prod = os.environ.get("CLAUDE_BOX_HOST_PRODUCT", "").strip()
    if man:
        inv["manufacturer"] = man
    if prod:
        inv["product"] = prod
    return inv


def _has_allowed_field(inv: dict[str, str], field: str) -> bool:
    return any(classify_key(k) == field for k in inv)


def load_inventory_from_sources(prefix: Path) -> dict[str, str]:
    """Load host inventory.

    Priority:
    1. Raw inventory (CLAUDE_BOX_HOST_INVENTORY or var/host-inventory.raw.yaml)
       — may contain forbidden keys, which sanitize() will drop.
       Env fills only missing manufacturer/product.
    2. Else CLAUDE_BOX_HOST_MANUFACTURER / CLAUDE_BOX_HOST_PRODUCT from config/env.
    3. Else var/host-identity.allowed.yaml (already sanitized output).
    """
    raw_candidates = []
    env_path = os.environ.get("CLAUDE_BOX_HOST_INVENTORY", "").strip()
    if env_path:
        raw_candidates.append(Path(env_path))
    raw_candidates.append(prefix / "var" / "host-inventory.raw.yaml")
    for c in raw_candidates:
        if c.is_file():
            inv = parse_flat_yaml(c)
            for field, val in _env_model().items():
                if not _has_allowed_field(inv, field):
                    inv[field] = val
            return inv

    env_inv = _env_model()
    if env_inv:
        return env_inv

    allowed = prefix / "var" / "host-identity.allowed.yaml"
    if allowed.is_file():
        return parse_flat_yaml(allowed)
    return {}


def cmd_apply(prefix: Path) -> int:
    hide_identifiers_required()
    inv = load_inventory_from_sources(prefix)
    allowed = sanitize(inv)
    write_presentation(prefix, allowed)
    man = allowed.get("manufacturer", "") or "(empty)"
    prod = allowed.get("product", "") or "(empty)"
    dropped = len(forbidden_values(inv))
    print(f"[claude-box] host-identity applied manufacturer={man} product={prod}")
    print(f"[claude-box] dropped {dropped} forbidden identifier value(s) from inventory/env")
    print(f"[claude-box] presentation → {prefix / 'presentation' / 'dmi' / 'id'}")
    print("[claude-box] note: live /sys/class/dmi is not rewritten")
    return 0


def cmd_verify(prefix: Path) -> int:
    hide_identifiers_required()
    errors: list[str] = []

    env_file = prefix / "env" / "host-model.env"
    dmi = prefix / "presentation" / "dmi" / "id"
    if not env_file.is_file():
        errors.append(f"missing {env_file}")
    if not (dmi / "sys_vendor").is_file() or not (dmi / "product_name").is_file():
        errors.append(f"missing DMI presentation under {dmi}")

    inv: dict[str, str] = {}
    inventory = os.environ.get("CLAUDE_BOX_HOST_INVENTORY", "").strip()
    raw = Path(inventory) if inventory else prefix / "var" / "host-inventory.raw.yaml"
    if raw.is_file():
        inv = parse_flat_yaml(raw)

    expected = sanitize(inv)
    # If no raw inventory, expected model comes from env / allowed file
    if not expected:
        expected = sanitize(load_inventory_from_sources(prefix))

    if env_file.is_file():
        text = env_file.read_text(encoding="utf-8")
        # Must not export identifier keys
        for line in text.splitlines():
            stripped = line.strip()
            if stripped.startswith("#") or "export " not in stripped:
                continue
            m = _FORBIDDEN_ASSIGN_RE.match(stripped)
            if m:
                errors.append(f"host-model.env exports identifier-like var {m.group(1)}")

    if expected.get("manufacturer"):
        got = (dmi / "sys_vendor").read_text(encoding="utf-8").strip() if (dmi / "sys_vendor").is_file() else ""
        if got != expected["manufacturer"]:
            errors.append(f"sys_vendor {got!r} != expected manufacturer {expected['manufacturer']!r}")
        if env_file.is_file() and expected["manufacturer"] not in env_file.read_text(encoding="utf-8"):
            errors.append("host-model.env missing manufacturer")
    if expected.get("product"):
        got = (dmi / "product_name").read_text(encoding="utf-8").strip() if (dmi / "product_name").is_file() else ""
        if got != expected["product"]:
            errors.append(f"product_name {got!r} != expected product {expected['product']!r}")
        if env_file.is_file() and expected["product"] not in env_file.read_text(encoding="utf-8"):
            errors.append("host-model.env missing product")

    leaks = scan_leaks(prefix, forbidden_values(inv) if inv else [])
    errors.extend(leaks)

    # Even without a raw fixture, published model fields must not look like IDs
    for name in ("sys_vendor", "product_name"):
        p = dmi / name
        if p.is_file():
            kind = looks_like_identifier(p.read_text(encoding="utf-8").strip())
            if kind:
                errors.append(f"{p} looks like a {kind}")

    if errors:
        print("[claude-box] verify-host-identity FAIL", file=sys.stderr)
        for e in errors:
            print(f"  - {e}", file=sys.stderr)
        return 1

    print("[claude-box] verify-host-identity OK")
    if expected.get("manufacturer") or expected.get("product"):
        print(
            f"  manufacturer={expected.get('manufacturer', '')!s} "
            f"product={expected.get('product', '')!s}"
        )
    else:
        print("  no model configured (identifier hiding still enforced)")
    print("  identifiers not present in presentation layer / advertised env")
    return 0


def main(argv: list[str]) -> int:
    if len(argv) < 2 or argv[0] not in {"apply", "verify"}:
        print("usage: host-identity.py apply|verify <prefix>", file=sys.stderr)
        return 2
    prefix = Path(argv[1])
    if argv[0] == "apply":
        return cmd_apply(prefix)
    return cmd_verify(prefix)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
