#!/usr/bin/env bash
set -euo pipefail
TV_IP="${1:?Usage: scripts/install-to-tv.sh <TV_IP> [wgt]}"
WGT="${2:-app/release/AirTizen.wgt}"
sdb connect "$TV_IP"
tizen install -n "$WGT"
