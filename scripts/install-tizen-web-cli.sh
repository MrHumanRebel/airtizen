#!/usr/bin/env bash
set -euo pipefail
# The official wrapper writes to $HOME. Run its verified embedded installer with
# a JVM-specific, isolated user.home, leaving the shell HOME and user files intact.
project_root="$(cd "$(dirname "$0")/.." && pwd)"
tizen_user_root="${AIRTIZEN_TIZEN_USER_ROOT:-$(dirname "$project_root")/tizen-user}"
tizen_cache="${AIRTIZEN_DEPS_DIR:-$(dirname "$project_root")/.airtizen-deps}"
installer="$tizen_cache/tizen-installer"
bootstrap="$tizen_cache/tizen-bootstrap"
if [ -x "$tizen_user_root/tizen-studio/tools/ide/bin/tizen" ]; then
  "$tizen_user_root/tizen-studio/tools/ide/bin/tizen" version
  exit 0
fi
mkdir -p "$tizen_cache" "$tizen_user_root"
if [ ! -s "$installer" ]; then
  curl --fail --location --retry 3 --proto '=https' -o "$installer" \
    https://download.tizen.org/sdk/Installer/tizen-studio_4.5.1/web-cli_Tizen_Studio_4.5.1_ubuntu-64.bin
fi
python3 - "$installer" "$bootstrap" <<'PY'
from pathlib import Path
import hashlib,io,sys,tarfile
archive=Path(sys.argv[1]).read_bytes()
expected='43d7961648d1fc6d8cb8fe764c8a2605178910fba744bb894b319301f3bfe7f9'
if hashlib.sha256(archive).hexdigest()!=expected: raise SystemExit('Tizen installer SHA256 mismatch')
payload=archive.split(b'\n',170)[170]
if hashlib.md5(payload).hexdigest()!='bd08ee5237c4876805e0cf909f369b59': raise SystemExit('Tizen embedded payload checksum mismatch')
destination=Path(sys.argv[2]);destination.mkdir(parents=True,exist_ok=True)
with tarfile.open(fileobj=io.BytesIO(payload),mode='r:gz') as source:
    for member in source.getmembers():
        target=(destination/member.name).resolve()
        if not target.is_relative_to(destination.resolve()) or member.issym() or member.islnk():
            raise SystemExit('Unsafe installer archive entry')
    source.extractall(destination,filter='data')
PY
unzip -q -o "$bootstrap/tizen-sdk.zip" 'jdk/*' -d "$bootstrap"
cd "$bootstrap"
"$bootstrap/jdk/bin/java" "-Duser.home=$tizen_user_root" -jar installer.jar \
  --accept-license "$tizen_user_root/tizen-studio"
"$tizen_user_root/tizen-studio/tools/ide/bin/tizen" version
