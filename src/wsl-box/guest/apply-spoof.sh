#!/usr/bin/env bash
# Apply TZ / locale spoof inside the ClaudeBox distro (env-first; no asar patch),
# then publish host manufacturer/product (never hardware identifiers).
# Idempotent. Non-root: session env only (+ optional CLAUDE_BOX_PREFIX persistence).
# Root: also /etc/localtime, /etc/timezone, /etc/profile.d/claude-box-spoof.sh.
# Host model is written to $CLAUDE_BOX_PREFIX/presentation and env/host-model.env;
# live /sys/class/dmi is not rewritten.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

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
# Box hostname only — never the host's real hostname (do not call hostname(1)).
SENTRY_NAME="${SENTRY_NAME:-${CLAUDE_BOX_HOSTNAME:-claude-box}}"
CLAUDE_BOX_HOSTNAME="${CLAUDE_BOX_HOSTNAME:-$SENTRY_NAME}"
export SENTRY_NAME
export CLAUDE_BOX_HOSTNAME

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
  mkdir -p "$PREFIX/env" "$PREFIX/etc/zoneinfo"
  ICU_TIMEZONE_FILES_DIR="${ICU_TIMEZONE_FILES_DIR:-$PREFIX/etc/zoneinfo}"
  export ICU_TIMEZONE_FILES_DIR
  if [[ -e "/usr/share/zoneinfo/${TZ_VALUE}" ]]; then
    mkdir -p "$ICU_TIMEZONE_FILES_DIR/$(dirname "$TZ_VALUE")"
    # Copy only the spoof timezone; do not mirror the host's other zones.
    cp -fL "/usr/share/zoneinfo/${TZ_VALUE}" "$ICU_TIMEZONE_FILES_DIR/${TZ_VALUE}" 2>/dev/null       || cp -f "/usr/share/zoneinfo/${TZ_VALUE}" "$ICU_TIMEZONE_FILES_DIR/${TZ_VALUE}"
  fi
  cat > "$PREFIX/env/spoof.env" << INNER
export TZ="$TZ_VALUE"
export LANG="$LOCALE_VALUE"
export LC_ALL="$LOCALE_VALUE"
export LANGUAGE="$LANGUAGE_VALUE"
export SENTRY_NAME="$SENTRY_NAME"
export CLAUDE_BOX_HOSTNAME="$CLAUDE_BOX_HOSTNAME"
export ICU_TIMEZONE_FILES_DIR="$ICU_TIMEZONE_FILES_DIR"
INNER
  echo "[claude-box] wrote $PREFIX/env/spoof.env"
fi

# Host identity: real manufacturer/product may be published; identifiers never are.
# TZ/locale spoofing is not relaxed when a real model is present.
if [[ -x "$SCRIPT_DIR/apply-host-identity.sh" ]]; then
  bash "$SCRIPT_DIR/apply-host-identity.sh"
fi

echo "[claude-box] apply-spoof done"
date
