#!/usr/bin/env bash
set -euo pipefail
SERVICE1="${1:-_raop._tcp.local}"
SERVICE2="${2:-_airplay._tcp.local}"
if command -v avahi-browse >/dev/null 2>&1; then
  timeout 6 avahi-browse -rt "$SERVICE1" || true
  timeout 6 avahi-browse -rt "$SERVICE2" || true
elif command -v dns-sd >/dev/null 2>&1; then
  timeout 6 dns-sd -B "$SERVICE1" || true
  timeout 6 dns-sd -B "$SERVICE2" || true
else
  echo "need avahi-browse or dns-sd" >&2
  exit 1
fi
