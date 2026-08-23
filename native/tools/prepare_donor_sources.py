#!/usr/bin/env python3
"""Create UTF-8 build copies of legacy UTF-16 AirPlayServerLib sources."""
from pathlib import Path
import sys

if len(sys.argv) != 3:
    raise SystemExit("usage: prepare_donor_sources.py AIRPLAY_LIB_DIR OUTPUT_DIR")
source = Path(sys.argv[1])
out = Path(sys.argv[2])
out.mkdir(parents=True, exist_ok=True)

def read(name: str) -> str:
    data = (source / name).read_bytes()
    return data.decode("utf-16") if data.startswith((b"\xff\xfe", b"\xfe\xff")) else data.decode("utf-8")

def write(name: str, text: str) -> None:
    (out / name).write_text(text.replace("\r\n", "\n"), encoding="utf-8")

handlers = read("raop_handlers.h").replace('"plist/include/plist.h"', '"plist/plist/plist.h"')
write("raop_handlers.generated.h", handlers)

raop = read("raop.c").replace('"raop_handlers.h"', '"raop_handlers.generated.h"')
write("raop.generated.c", raop)

buffer = read("raop_buffer.c").replace("#include <sha512.h>", '#include "ed25519/sha512.h"')
write("raop_buffer.generated.c", buffer)

mirror = read("raop_rtp_mirror.c")
mirror = mirror.replace("const char remoteName[128];", "char remoteName[128];")
mirror = mirror.replace("const char remoteDeviceId[128];", "char remoteDeviceId[128];")
mirror = mirror.replace(
    "strncpy(raop_rtp_mirror->remoteName, remoteName, min(128, strlen(remoteName)));",
    'snprintf(raop_rtp_mirror->remoteName, sizeof(raop_rtp_mirror->remoteName), "%s", remoteName);',
)
mirror = mirror.replace(
    "strncpy(raop_rtp_mirror->remoteDeviceId, remoteDeviceId, min(128, strlen(remoteDeviceId)));",
    'snprintf(raop_rtp_mirror->remoteDeviceId, sizeof(raop_rtp_mirror->remoteDeviceId), "%s", remoteDeviceId);',
)
mirror = mirror.replace("thread_exit_exception != NULL", "thread_exit_exception != 0")
mirror = mirror.replace("thread_exit_exception = NULL", "thread_exit_exception = 0")
write("raop_rtp_mirror.generated.c", mirror)
