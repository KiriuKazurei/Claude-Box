#!/usr/bin/env bash
# Minimal YAML scalar / list reader for wsl-box.example.yaml shape (no PyYAML).
# shellcheck shell=bash

# Usage: yaml_scalar <file> <key> [default]
yaml_scalar() {
  local file="$1" key="$2" default="${3:-}"
  local val
  val="$(python3 - "$file" "$key" <<'PY'
import re, sys
path, key = sys.argv[1], sys.argv[2]
text = open(path, encoding="utf-8").read()
pat = re.compile(rf"(?m)^\s*{re.escape(key)}:\s*[\"']?([^\"'#\r\n]+)")
m = pat.search(text)
if m:
    print(m.group(1).strip())
PY
)" || true
  if [[ -z "$val" ]]; then
    printf '%s\n' "$default"
  else
    printf '%s\n' "$val"
  fi
}

# Usage: yaml_blocklist <file> → one host per line under gateway.blocklist
yaml_blocklist() {
  local file="$1"
  python3 - "$file" <<'PY'
import re, sys
text = open(sys.argv[1], encoding="utf-8").read()
m = re.search(r"(?m)^(\s*)blocklist:\s*(?:\[(.*?)\]\s*)?(?:#.*)?$", text)
if not m:
    sys.exit(0)
indent, inline = m.group(1), m.group(2)
seen = []
def add(x):
    x = x.strip().strip("\"'")
    if x and x not in seen:
        seen.append(x)
        print(x)
if inline is not None and inline.strip():
    for part in inline.split(","):
        add(part)
    sys.exit(0)
base = len(indent)
for line in text[m.end():].splitlines():
    if not line.strip() or line.strip().startswith("#"):
        continue
    leading = len(line) - len(line.lstrip(" "))
    if leading <= base and not line.lstrip().startswith("-"):
        break
    mm = re.match(r"^\s*-\s*[\"']?([^\"'#\r\n]+)", line)
    if mm:
        add(mm.group(1))
PY
}

# Export CLAUDE_BOX_* from config for guest scripts / smoke.
# Usage: eval "$(load_config_exports "$cfg")"
load_config_exports() {
  local cfg="$1"
  local tz locale language elang host port cmd kill_ip oauth_fp vault bl_csv
  tz="$(yaml_scalar "$cfg" timezone America/Los_Angeles)"
  locale="$(yaml_scalar "$cfg" locale en_US.UTF-8)"
  language="$(yaml_scalar "$cfg" language en_US)"
  elang="$(yaml_scalar "$cfg" electron_lang en-US)"
  host="$(yaml_scalar "$cfg" host auto)"
  port="$(yaml_scalar "$cfg" http_port 3080)"
  cmd="$(yaml_scalar "$cfg" command claude-desktop)"
  kill_ip="$(yaml_scalar "$cfg" kill_on_local_ip_change false)"
  oauth_fp="$(yaml_scalar "$cfg" oauth_open_in_fingerprint_browser true)"
  vault="$(yaml_scalar "$cfg" identity_vault true)"
  bl_csv="$(yaml_blocklist "$cfg" | paste -sd, -)"

  cat <<EXP
export TZ=$(printf '%q' "$tz")
export LANG=$(printf '%q' "$locale")
export LC_ALL=$(printf '%q' "$locale")
export LANGUAGE=$(printf '%q' "$language")
export CLAUDE_BOX_ELECTRON_LANG=$(printf '%q' "$elang")
export CLAUDE_BOX_DESKTOP_CMD=$(printf '%q' "$cmd")
export CLAUDE_BOX_GATEWAY_HOST=$(printf '%q' "$host")
export CLAUDE_BOX_GATEWAY_PORT=$(printf '%q' "$port")
export CLAUDE_BOX_BLOCKLIST=$(printf '%q' "$bl_csv")
export CLAUDE_BOX_KILL_ON_LOCAL_IP_CHANGE=$(printf '%q' "$kill_ip")
export CLAUDE_BOX_OAUTH_OPEN_IN_FINGERPRINT_BROWSER=$(printf '%q' "$oauth_fp")
export CLAUDE_BOX_IDENTITY_VAULT=$(printf '%q' "$vault")
EXP
}
