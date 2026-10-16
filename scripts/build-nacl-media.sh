#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
cache="${AIRTIZEN_DEPS_DIR:-$(dirname "$root")/.airtizen-deps}"
export NACL_SDK_ROOT="${NACL_SDK_ROOT:-$cache/nacl-sdk/pepper_47}"
export PNACLPYTHON="${PNACLPYTHON:-$cache/python2/bin/python2.7}"
revision=3a0867c2bfda4a4d4309ca1a8cbdc6175e67f587
prefix="$cache/media-nacl"
if [ -s "$prefix/.airtizen-revision" ] && [ "$(cat "$prefix/.airtizen-revision")" = "$revision:nacl-arm-aac-v2" ]; then exit 0; fi
archive="$cache/ffmpeg-7.1.5.tar.gz"
if [ ! -s "$archive" ]; then
  curl --fail --location --retry 3 --proto '=https' -o "$archive" "https://codeload.github.com/FFmpeg/FFmpeg/tar.gz/$revision"
fi
printf '%s  %s\n' 83220fe0807da10df19714176f02ec3d3812ef0e055f69e940d01b3ad1379cdb "$archive" | sha256sum -c -
mkdir -p "$cache/nacl-media-source" "$cache/media-nacl-build"
if [ ! -s "$cache/nacl-media-source/FFmpeg-$revision/configure" ]; then
  tar -xzf "$archive" -C "$cache/nacl-media-source"
fi
cd "$cache/media-nacl-build"
bin="$NACL_SDK_ROOT/toolchain/linux_pnacl/bin"
# This SDK's stdatomic.h omits stdint.h. Include the required standard types;
# retain real compiler atomics and pthread support rather than dummy atomics.
"$cache/nacl-media-source/FFmpeg-$revision/configure" --prefix="$prefix" \
  --cc="$bin/arm-nacl-clang" --ar="$bin/arm-nacl-ar" --ranlib="$bin/arm-nacl-ranlib" \
  --target-os=none --arch=arm --cpu=generic --enable-cross-compile \
  --extra-cflags='-include stdint.h' \
  --disable-everything --disable-autodetect --disable-programs --disable-doc \
  --disable-network --disable-asm --disable-avdevice --disable-avfilter \
  --disable-swscale --disable-swresample --enable-pthreads --disable-w32threads --disable-os2threads \
  --enable-decoder=alac,h264,aac --enable-parser=h264 --enable-muxer=mp4 --enable-static --disable-shared
make -j"${JOBS:-2}"
make install
printf '%s\n' "$revision:nacl-arm-aac-v2" > "$prefix/.airtizen-revision"
