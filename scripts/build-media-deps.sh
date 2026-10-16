#!/usr/bin/env bash
set -euo pipefail
# Minimal LGPL FFmpeg build from an exact official upstream revision.
project_root="$(cd "$(dirname "$0")/.." && pwd)"
media_cache="${AIRTIZEN_DEPS_DIR:-$(dirname "$project_root")/.airtizen-deps}"
media_prefix="${AIRTIZEN_MEDIA_PREFIX:-$media_cache/media}"
media_commit=3a0867c2bfda4a4d4309ca1a8cbdc6175e67f587 # upstream n7.1.5
media_sha256=83220fe0807da10df19714176f02ec3d3812ef0e055f69e940d01b3ad1379cdb
media_source="$media_cache/FFmpeg-$media_commit"
media_archive="$media_cache/ffmpeg-7.1.5.tar.gz"
if [ -f "$media_prefix/.airtizen-revision" ] && [ "$(cat "$media_prefix/.airtizen-revision")" = "$media_commit:mp4-aac-v2" ] && [ -s "$media_prefix/lib/pkgconfig/libavcodec.pc" ]; then
  exit 0
fi
mkdir -p "$media_cache"
if [ ! -s "$media_archive" ]; then
  curl --fail --location --retry 3 --proto '=https' -o "$media_archive" \
    "https://codeload.github.com/FFmpeg/FFmpeg/tar.gz/$media_commit"
fi
printf '%s  %s\n' "$media_sha256" "$media_archive" | sha256sum -c -
if [ ! -f "$media_source/configure" ]; then tar -xzf "$media_archive" -C "$media_cache"; fi
cd "$media_source"
./configure --prefix="$media_prefix" --disable-everything --disable-autodetect \
  --disable-programs --disable-doc --disable-network --disable-x86asm \
  --disable-avdevice --disable-avfilter --disable-swscale --disable-swresample \
  --enable-decoder=alac,h264,aac --enable-parser=h264 --enable-muxer=mp4 --enable-pic --enable-static --disable-shared
make -j"${JOBS:-2}"
make install
printf '%s\n' "$media_commit:mp4-aac-v2" > "$media_prefix/.airtizen-revision"
