#!/usr/bin/env bash
set -euo pipefail
mkdir -p build
cd build
emcmake cmake ..
emmake make -j2
