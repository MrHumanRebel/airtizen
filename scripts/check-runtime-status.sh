#!/usr/bin/env bash
set -euo pipefail
URL="${AIRTIZEN_RUNTIME_URL:-http://127.0.0.1:45110/status}"
echo "[airtizen] query: $URL"
json="$(curl -fsS --max-time 3 "$URL")"
echo "$json"
echo "$json" | python3 -c 'import json,sys;d=json.load(sys.stdin);assert d.get("ok") is True;assert "service" in d;assert "protocols" in d;assert "airplay" in d["protocols"];print("[airtizen] runtime status schema ok")'
