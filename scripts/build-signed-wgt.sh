#!/usr/bin/env bash
set -euo pipefail
if [ "$#" -ne 2 ]; then echo 'usage: build-signed-wgt.sh STAGED_UI OUTPUT_DIR' >&2; exit 2; fi
stage_dir="$(realpath "$1")"
output_dir="$(realpath -m "$2")"
: "${GITHUB_WORKSPACE:?repository root is required}"
: "${RUNNER_TEMP:?temporary directory is required}"
: "${TIZEN_PROFILE_NAME:=AirTizen}"
export TIZEN_STUDIO_ROOT="${TIZEN_STUDIO_ROOT:-$GITHUB_WORKSPACE/tizen-studio}"
tizen_bin="$TIZEN_STUDIO_ROOT/tools/ide/bin/tizen"
test -x "$tizen_bin";test -s "$stage_dir/config.xml"
if [ "$stage_dir" = "$GITHUB_WORKSPACE/app" ]; then echo 'Build a staged copy, not tracked app/' >&2; exit 1; fi
mkdir -p "$output_dir"
if [ -n "$(find "$output_dir" -maxdepth 1 -name '*.wgt' -print -quit)" ]; then echo 'Choose a fresh signing output directory' >&2; exit 1; fi
sign_dir="$(mktemp -d "$RUNNER_TEMP/airtizen-sign.XXXXXX")"
umask 077
"$tizen_bin" cli-config -l > "$sign_dir/config-before.txt" 2>&1
original_profiles="$(python3 - "$sign_dir/config-before.txt" <<'PY'
from pathlib import Path
import sys
for line in Path(sys.argv[1]).read_text().splitlines():
 key,separator,value=line.partition('=')
 if separator and key.strip()=='profiles.path':print(value.strip());break
PY
)"
original_profiles="${original_profiles:-$TIZEN_STUDIO_ROOT-data/profile/profiles.xml}"
cleanup() {
  "$tizen_bin" cli-config "profiles.path=$original_profiles" >/dev/null 2>&1 || true
  rm -rf "$sign_dir"
}
trap cleanup EXIT
if [ -n "${TIZEN_CERT_P12_BASE64:-}" ]; then
  : "${TIZEN_CERT_PASSWORD:?configured author P12 requires its password}"
  printf '%s' "$TIZEN_CERT_P12_BASE64" | base64 --decode > "$sign_dir/author.p12"
  author_mode=configured-author
else
  if [ -n "${TIZEN_CERT_PASSWORD:-}" ] && [ "$TIZEN_CERT_PASSWORD" != 1234 ]; then
    echo 'Author password without an author P12: supply both, or use the public community default.' >&2; exit 1
  fi
  cp "$GITHUB_WORKSPACE/certificates/community-author.p12" "$sign_dir/author.p12"
  export TIZEN_CERT_PASSWORD=1234
  author_mode=public-community-author
fi
verify_options=()
if [ -n "${TIZEN_DISTRIBUTOR_P12_BASE64:-}" ]; then
  : "${TIZEN_DISTRIBUTOR_PASSWORD:?configured distributor P12 requires its password}"
  printf '%s' "$TIZEN_DISTRIBUTOR_P12_BASE64" | base64 --decode > "$sign_dir/distributor.p12"
  distributor_mode=configured-distributor
else
  if [ -n "${TIZEN_DISTRIBUTOR_PASSWORD:-}" ] && [ "$TIZEN_DISTRIBUTOR_PASSWORD" != tizenpkcs12passfordsigner ]; then
    echo 'Distributor password requires a distributor P12.' >&2; exit 1
  fi
  cp "$TIZEN_STUDIO_ROOT/tools/certificate-generator/certificates/distributor/tizen-distributor-signer.p12" "$sign_dir/distributor.p12"
  export TIZEN_DISTRIBUTOR_PASSWORD=tizenpkcs12passfordsigner
  distributor_mode=public-sdk-distributor
  verify_options+=(--allow-sdk-distributor)
fi
"$tizen_bin" cli-config "profiles.path=$sign_dir/profiles.xml"
"$tizen_bin" security-profiles add -f -n "$TIZEN_PROFILE_NAME" \
  -a "$sign_dir/author.p12" -p "$TIZEN_CERT_PASSWORD" \
  -d "$sign_dir/distributor.p12" -dp "$TIZEN_DISTRIBUTOR_PASSWORD"
# This old CLI can return zero even when profile creation failed. Validate it
# and replace desktop-keyring password references in this private temp file.
python3 "$GITHUB_WORKSPACE/scripts/prepare-signing-profile.py" "$sign_dir/profiles.xml" "$TIZEN_PROFILE_NAME"
"$tizen_bin" build-web -- "$stage_dir"
test -d "$stage_dir/.buildResult"
python3 "$GITHUB_WORKSPACE/scripts/sign-wgt.py" "$tizen_bin" "$stage_dir/.buildResult" "$output_dir" "$TIZEN_PROFILE_NAME"
mapfile -t packages < <(find "$output_dir" -maxdepth 1 -type f -name '*.wgt')
if [ "${#packages[@]}" -ne 1 ]; then echo 'Expected exactly one signed WGT' >&2; exit 1; fi
if [ "${packages[0]}" != "$output_dir/AirTizen.wgt" ]; then mv "${packages[0]}" "$output_dir/AirTizen.wgt"; fi
version="$(python3 - "$stage_dir/config.xml" <<'PY'
import sys,xml.etree.ElementTree as ET
print(ET.parse(sys.argv[1]).getroot().get('version'))
PY
)"
python3 "$GITHUB_WORKSPACE/scripts/verify-wgt.py" "$output_dir/AirTizen.wgt" "$version" "${verify_options[@]}"
printf '%s\n%s\n' "$author_mode" "$distributor_mode" > "$output_dir/SIGNING-MODE.txt"
