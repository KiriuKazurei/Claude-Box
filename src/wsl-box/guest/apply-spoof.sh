#!/usr/bin/env bash
# Apply TZ / locale spoof inside the ClaudeBox distro (env-first; no asar patch).
# Idempotent. Non-root: session env only (+ optional CLAUDE_BOX_PREFIX persistence).
# Root: also /etc/localtime, /etc/timezone, /etc/profile.d/claude-box-spoof.sh.
set -euo pipefail

TZ_VALUE="${TZ:-America/Los_Angeles}"
LOCALE_VALUE="${LANG:-en_US.UTF-8}"
LANGUAGE_VALUE="${LANGUAGE:-en_US}"
PREFIX="${CLAUDE_BOX_PREFIX:-}"

echo "[claude-box] apply-spoof TZ=$TZ_VALUE locale=$LOCALE_VALUE"

if [[ -d /usr/share/zoneinfo ]]; then
  ZONE_FILE="/usr/share/zoneinfo/${TZ_VALUE}"
  if [[ -e "$ZONE_FILE" ]]; then
    if [[ "$(id -u)" -eq 0 ]]; then
      ln -sfn "$ZONE_FILE" /etc/localtime
      echo "$TZ_VALUE" > /etc/timezone 2>/dev/null || true
    else
      echo "warn: not root; skip /etc/localtime (session TZ still set)"
      echo "      root path: sudo TZ=$TZ_VALUE $0"
    fi
  else
    echo "warn: zoneinfo missing for $TZ_VALUE"
  fi
fi

export TZ="$TZ_VALUE"
export LANG="$LOCALE_VALUE"
export LC_ALL="$LOCALE_VALUE"
export LANGUAGE="$LANGUAGE_VALUE"

# Persist for login shells when root
if [[ "$(id -u)" -eq 0 ]]; then
  mkdir -p /etc/profile.d
  cat > /etc/profile.d/claude-box-spoof.sh << INNER
export TZ="$TZ_VALUE"
export LANG="$LOCALE_VALUE"
export LC_ALL="$LOCALE_VALUE"
export LANGUAGE="$LANGUAGE_VALUE"
INNER
fi

# Non-root / cloud prefix: write spoof.env for later source (idempotent)
if [[ -n "$PREFIX" ]]; then
  mkdir -p "$PREFIX/env"
  cat > "$PREFIX/env/spoof.env" << INNER
export TZ="$TZ_VALUE"
export LANG="$LOCALE_VALUE"
export LC_ALL="$LOCALE_VALUE"
export LANGUAGE="$LANGUAGE_VALUE"
INNER
  echo "[claude-box] wrote $PREFIX/env/spoof.env"
fi

echo "[claude-box] apply-spoof done"
date
