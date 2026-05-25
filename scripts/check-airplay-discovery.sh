#!/usr/bin/env bash
set -euo pipefail
bash "$(dirname "$0")/check-mdns.sh" _raop._tcp.local _airplay._tcp.local
