#!/usr/bin/env bash
# Apply TZ / locale spoof inside the ClaudeBox distro (env-first; no asar patch).
set -euo pipefail

TZ_VALUE="${TZ:-America/Los_Angeles}"
LOCALE_VALUE="${LANG:-en_US.UTF-8}"
LANGUAGE_VALUE="${LANGUAGE:-en_US}"

echo "[claude-box] apply-spoof TZ=$TZ_VALUE locale=$LOCALE_VALUE"

if [[ -d /usr/share/zoneinfo ]]; then
  ZONE_FILE="/usr/share/zoneinfo/${TZ_VALUE}"
  if [[ -e "$ZONE_FILE" ]]; then
    if [[ "$(id -u)" -eq 0 ]]; then
      ln -sfn "$ZONE_FILE" /etc/localtime
      echo "$TZ_VALUE" > /etc/timezone 2>/dev/null || true
    else
      echo "warn: not root; skip /etc/localtime (session TZ still set)"
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

echo "[claude-box] apply-spoof done"
date
