#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
cache="${AIRTIZEN_DEPS_DIR:-$(dirname "$root")/.airtizen-deps}"
export NACL_SDK_ROOT="${NACL_SDK_ROOT:-$cache/nacl-sdk/pepper_47}"
export PNACLPYTHON="${PNACLPYTHON:-$cache/python2/bin/python2.7}"
bash "$root/scripts/install-nacl-sdk.sh"
bash "$root/scripts/build-nacl-media.sh"
build="$cache/receiver-nacl-build"
export PKG_CONFIG_LIBDIR="$cache/media-nacl/lib/pkgconfig"
cmake -S "$root/native" -B "$build" -DCMAKE_TOOLCHAIN_FILE="$root/native/nacl/toolchain.cmake" \
  -DAIRTIZEN_BUILD_NACL_MODULE=ON -DAIRTIZEN_MEDIA_PREFIX="$cache/media-nacl" \
  -DBUILD_TESTING=OFF -DCMAKE_BUILD_TYPE=Release -DPython3_EXECUTABLE="$(command -v python3)"
cmake --build "$build" -j"${JOBS:-2}"
module="$build/airtizen_receiver.nexe"
python3 "$root/scripts/check-nacl-sockets.py" "$module"
"$NACL_SDK_ROOT/tools/ncval" "$module"
"$NACL_SDK_ROOT/toolchain/linux_pnacl/bin/arm-nacl-strip" --strip-debug "$module"
"$NACL_SDK_ROOT/tools/ncval" "$module"
printf 'Validated ARM NaCl receiver: %s\n' "$module"
