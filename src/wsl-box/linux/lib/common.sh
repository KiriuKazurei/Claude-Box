#!/usr/bin/env bash
# Shared helpers for cloud-Linux / dry-run framework (no WSL required).
# shellcheck shell=bash

claude_box_repo_root() {
  # linux/lib → wsl-box → src → repo
  cd "$(dirname "${BASH_SOURCE[0]}")/../../../.." && pwd
}

claude_box_log() {
  printf '[claude-box/linux] %s\n' "$*"
}

claude_box_warn() {
  printf '[claude-box/linux] warn: %s\n' "$*" >&2
}

claude_box_die() {
  printf '[claude-box/linux] error: %s\n' "$*" >&2
  exit 1
}

# Default project-local prefix (gitignored). Override with CLAUDE_BOX_PREFIX.
claude_box_default_prefix() {
  local root
  root="$(claude_box_repo_root)"
  printf '%s\n' "${root}/.run/claude-box"
}

claude_box_resolve_config() {
  local root cfg example
  root="$(claude_box_repo_root)"
  cfg="${CLAUDE_BOX_CONFIG:-}"
  if [[ -n "$cfg" && -f "$cfg" ]]; then
    printf '%s\n' "$cfg"
    return 0
  fi
  if [[ -f "${root}/src/wsl-box/config/wsl-box.yaml" ]]; then
    printf '%s\n' "${root}/src/wsl-box/config/wsl-box.yaml"
    return 0
  fi
  example="${root}/src/wsl-box/config/wsl-box.example.yaml"
  if [[ -f "$example" ]]; then
    printf '%s\n' "$example"
    return 0
  fi
  return 1
}
