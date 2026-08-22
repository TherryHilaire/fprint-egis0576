#!/usr/bin/env bash
# One-time setup: store the KeePassXC database passphrase in the
# session keyring so unlock.sh can retrieve it after a fingerprint match.

set -euo pipefail

command -v secret-tool >/dev/null || {
  echo "secret-tool missing (Arch: sudo pacman -S libsecret)"
  exit 1
}

read -rsp "KeePassXC database passphrase (input hidden): " pass
echo

printf '%s' "$pass" | secret-tool store --label="KeePassXC (fingerprint)" keepassxc vault

echo "Stored in the session keyring as: keepassxc=vault"
echo "The keyring unlocks at desktop login (PAM), where your fingerprint works."
