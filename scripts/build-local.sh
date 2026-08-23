#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cmake -S "$ROOT/native" -B "$ROOT/native/build" -DAIRTIZEN_BUILD_TIZEN_SERVICE=OFF
cmake --build "$ROOT/native/build" -j"${JOBS:-2}"
