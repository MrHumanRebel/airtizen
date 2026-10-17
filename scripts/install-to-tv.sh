#!/usr/bin/env bash
set -euo pipefail
TV_IP="${1:?Usage: scripts/install-to-tv.sh <TV_IP> [wgt] [TV_TARGET_NAME]}"
WGT="$(realpath "${2:-app/release/AirTizen.wgt}")"
selection=(-s "$TV_IP:26101")
if [ -n "${3:-}" ]; then selection=(-t "$3"); fi
test -f "$WGT"
sdb connect "$TV_IP"
log="$(mktemp)"
trap 'rm -f "$log"' EXIT
status=0
tizen install -n "$(basename "$WGT")" "${selection[@]}" -- "$(dirname "$WGT")" >"$log" 2>&1 || status=$?
cat "$log"
if [ "$status" -ne 0 ] || grep -Eqi 'install failed|failed to install' "$log"; then
  echo 'Installation failed. Error 118 is generic; preserve the full platform log. Do not uninstall an existing app automatically.' >&2
  exit 1
fi
if ! grep -Eqi 'successfully installed|Installed the package' "$log"; then
  echo 'CLI returned without a confirmed installation success; inspect the platform log.' >&2
  exit 1
fi
