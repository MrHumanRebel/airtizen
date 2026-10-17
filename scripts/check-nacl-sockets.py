#!/usr/bin/env python3
"""Reject newlib's linkable-but-unimplemented socket stubs in the ARM module.

Symbol presence alone is insufficient: kernel_intercept.o brings ki_* symbols
even when the public POSIX functions still resolve to libc stubs. Inspect each
used public function's actual branch to its nacl_io implementation instead.
"""
import os
from pathlib import Path
import re
import subprocess
import sys


def check(module, sdk):
    tools = sdk / 'toolchain/linux_pnacl/bin'
    symbols = subprocess.check_output([str(tools / 'arm-nacl-nm'), str(module)], text=True)
    addresses = {name: int(address, 16) for address, name in
                 re.findall(r'^([0-9a-fA-F]+) T (\w+)$', symbols, re.M)}
    calls = ('socket', 'setsockopt', 'getsockopt', 'bind', 'getsockname',
             'getpeername', 'listen', 'accept', 'connect', 'select', 'recv',
             'recvfrom', 'send', 'sendto', 'shutdown', 'getaddrinfo', 'freeaddrinfo')
    missing = {'ki_socket', 'ki_bind', 'ki_getsockname', 'ki_listen'} - addresses.keys()
    if missing:
        raise ValueError('Missing receiver socket functions: ' + ', '.join(sorted(missing)))
    checked = []
    for name in calls:
        if name not in addresses:
            # PNaCl's final link optimizes the bitcode wrappers into callers.
            # Newlib's native ELF stubs cannot disappear through that pass.
            if 'ki_' + name in addresses:
                checked.append(name + ' (inlined)')
            continue
        start = addresses[name]
        # Bound inspection to this function, excluding the next function even
        # if it happens to contain the required ki_* branch.
        end = min((v for v in addresses.values() if v > start), default=start + 1024)
        assembly = subprocess.check_output([
            str(tools / 'arm-nacl-objdump'), '-d', '--start-address=' + hex(start),
            '--stop-address=' + hex(end), str(module)], text=True)
        if not re.search(r'\bb(?:l)?\s+[^\n]*<ki_' + name + r'>', assembly):
            raise ValueError(name + ' does not call nacl_io (possible libc ENOSYS stub); check static library order')
        checked.append(name)
    print('Verified ARM nacl_io dispatch: ' + ', '.join(checked))


if __name__ == '__main__':
    if len(sys.argv) != 2 or not os.environ.get('NACL_SDK_ROOT'):
        raise SystemExit('usage: NACL_SDK_ROOT=... check-nacl-sockets.py MODULE.nexe')
    try:
        check(Path(sys.argv[1]), Path(os.environ['NACL_SDK_ROOT']))
    except (ValueError, subprocess.CalledProcessError) as error:
        raise SystemExit(str(error))
