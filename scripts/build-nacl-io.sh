#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
cache="${AIRTIZEN_DEPS_DIR:-$(dirname "$root")/.airtizen-deps}"
: "${NACL_SDK_ROOT:?SDK required}"
out="$cache/nacl-io-fixed"
mkdir -p "$out"
python3 "$root/tests/test_samsung_udp.py" "$NACL_SDK_ROOT"
python3 "$root/scripts/prepare-nacl-udp.py" "$NACL_SDK_ROOT/src/nacl_io/socket/udp_node.cc" "$out/udp_node.cc"
bin="$NACL_SDK_ROOT/toolchain/linux_pnacl/bin"
"$bin/arm-nacl-clang++" -c "$out/udp_node.cc" -o "$out/udp_node_arm.o" \
  -O2 -pthread -DNDEBUG -DNACL_IO_LOGGING=0 \
  -I"$NACL_SDK_ROOT/include" -I"$NACL_SDK_ROOT/include/newlib"
cp "$NACL_SDK_ROOT/lib/clang-newlib_arm/Release/libnacl_io.a" "$out/libnacl_io.a"
"$bin/arm-nacl-ar" rcs "$out/libnacl_io.a" "$out/udp_node_arm.o"
