#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
bash "$ROOT/scripts/build-media-deps.sh"
export PKG_CONFIG_PATH="${AIRTIZEN_MEDIA_PREFIX:-${AIRTIZEN_DEPS_DIR:-$(dirname "$ROOT")/.airtizen-deps}/media}/lib/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
cmake -S "$ROOT/native" -B "$ROOT/native/build" -DAIRTIZEN_BUILD_TIZEN_SERVICE=OFF -DPython3_EXECUTABLE="$(command -v python3)"
cmake --build "$ROOT/native/build" -j"${JOBS:-2}"
