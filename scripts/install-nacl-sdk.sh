#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
cache="${AIRTIZEN_DEPS_DIR:-$(dirname "$root")/.airtizen-deps}"
sdk="${NACL_SDK_ROOT:-$cache/nacl-sdk/pepper_47}"
python2="${PNACLPYTHON:-$cache/python2/bin/python2.7}"
archive="$cache/samsung-pepper47.download"
sdk_hash=c6fe7bfbc1bf0981d018bea47e62574a9c7575d1b102281a8f728e00a09bab2e
mkdir -p "$cache"
if [ ! -f "$sdk/.airtizen-sdk-sha256" ] || [ "$(cat "$sdk/.airtizen-sdk-sha256")" != "$sdk_hash" ]; then
  if [ ! -s "$archive" ]; then
    curl --fail --location --retry 3 --proto '=https' -o "$archive" \
      https://developer.samsung.com/smarttv/file/b7e3cb3a-9b3d-4651-9404-3980dac50cdf
  fi
  printf '%s  %s\n' "$sdk_hash" "$archive" | sha256sum -c -
  if [ "$(basename "$sdk")" != pepper_47 ]; then echo 'SDK path must end in pepper_47' >&2; exit 1; fi
  mkdir -p "$(dirname "$sdk")"
  unzip -q -o "$archive" -d "$(dirname "$sdk")" -x 'pepper_47/toolchain/mac_*/*' 'pepper_47/toolchain/win_*/*'
  test -s "$sdk/include/ppapi/cpp/audio.h"
  test -x "$sdk/toolchain/linux_pnacl/bin/clang"
  printf '%s\n' "$sdk_hash" > "$sdk/.airtizen-sdk-sha256"
fi
# The 64-bit LLVM driver uses Python 2. Keep it isolated from Python 3 tests.
if [ ! -x "$python2" ]; then
  python_archive="$cache/Python-2.7.18.tar.xz"
  if [ ! -s "$python_archive" ]; then
    curl --fail --location --retry 3 --proto '=https' -o "$python_archive" \
      https://www.python.org/ftp/python/2.7.18/Python-2.7.18.tar.xz
  fi
  printf '%s  %s\n' b62c0e7937551d0cc02b8fd5cb0f544f9405bafc9a54d3808ed4594812edef43 "$python_archive" | sha256sum -c -
  tar -xJf "$python_archive" -C "$cache"
  cd "$cache/Python-2.7.18"
  ./configure --prefix="$(dirname "$(dirname "$python2")")" --disable-shared --without-ensurepip
  make -j"${JOBS:-2}"
  make install
fi
PNACLPYTHON="$python2" "$sdk/toolchain/linux_pnacl/bin/arm-nacl-clang" --version
