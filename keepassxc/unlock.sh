#!/usr/bin/env bash
# Unlock a KeePassXC database with the fingerprint reader.
#
# The database passphrase is stored once in the session keyring
# (see setup.sh). At unlock time we verify the fingerprint via fprintd,
# pull the passphrase from the keyring and feed it to KeePassXC.
#
# Configuration (optional, environment variables):
#   KEEPASSXC_DB    path to .kdbx        (default: ~/Documents/vault.kdbx)
#   KEEPASSXC_ATTR  keyring attr value   (default: vault)

set -euo pipefail

DB="${KEEPASSXC_DB:-$HOME/Documents/vault.kdbx}"
ATTR_VALUE="${KEEPASSXC_ATTR:-vault}"
MAX_TRIES=3

command -v keepassxc   >/dev/null || { echo "keepassxc missing   (Arch: sudo pacman -S keepassxc)"; exit 1; }
command -v secret-tool >/dev/null || { echo "secret-tool missing (Arch: sudo pacman -S libsecret)";     exit 1; }
command -v fprintd-verify >/dev/null || { echo "fprintd-verify missing (is fprintd installed?)";           exit 1; }

[ -f "$DB" ] || {
  echo "Database not found: $DB"
  echo "Set KEEPASSXC_DB=/path/to/database.kdbx or edit this script."
  exit 1
}

try=0
while :; do
  if fprintd-verify >/dev/null 2>&1; then
    pass="$(secret-tool lookup keepassxc "$ATTR_VALUE" || true)"
    if [ -z "$pass" ]; then
      echo "No stored passphrase found - run ./setup.sh once first."
      exit 1
    fi
    printf '%s' "$pass" | keepassxc --pw-stdin "$DB"
    exit 0
  fi

  try=$((try + 1))
  if [ "$try" -ge "$MAX_TRIES" ]; then
    break
  fi
  echo "Fingerprint not recognized ($try/$MAX_TRIES), try again..."
done

echo "Falling back to manual entry."
read -rsp "Database passphrase: " pass
echo
printf '%s' "$pass" | keepassxc --pw-stdin "$DB"
