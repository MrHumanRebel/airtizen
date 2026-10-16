"""Check the shipped receiver's files/architecture, independent of the SDK loader."""
import json
from pathlib import PurePosixPath
import struct


def check_module(data):
    if len(data) < 52 or data[:7] != b'\x7fELF\x01\x01\x01':
        raise ValueError('Receiver must be a 32-bit little-endian ARM ELF executable')
    kind, machine, version = struct.unpack_from('<HHI', data, 16)
    if kind != 2 or machine != 40 or version != 1:
        raise ValueError('Receiver is not an ARM executable')
    offset = struct.unpack_from('<I', data, 28)[0]
    size, count = struct.unpack_from('<HH', data, 42)
    if size != 32 or not count or count > 64 or offset + count * size > len(data):
        raise ValueError('Invalid receiver ELF program headers')
    entry = struct.unpack_from('<I', data, 24)[0]
    executable = False
    for index in range(count):
        segment, start, address, physical, files, memory, flags, alignment = struct.unpack_from('<IIIIIIII', data, offset + index * size)
        if segment == 1:
            if files > memory or start + files > len(data):
                raise ValueError('Truncated receiver ELF segment')
            if flags & 1 and address <= entry < address + memory:
                executable = True
    if not executable:
        raise ValueError('Receiver entry point is not in an executable segment')
    # This structural check does not replace the SDK ncval sandbox validator.


def check_embedded(read):
    manifest = json.loads(read('receiver/receiver.nmf'))
    architectures = manifest.get('program', {})
    if 'arm' not in architectures:
        raise ValueError('No ARM receiver in bundled NaCl manifest')
    for architecture, record in architectures.items():
        url = record.get('url', '')
        path = PurePosixPath(url)
        if not url or path.is_absolute() or '..' in path.parts or '\\' in url or ':' in url:
            raise ValueError('Receiver manifest references an external or unsafe module')
        data = read('receiver/' + url)
        if architecture == 'arm':
            check_module(data)
    if b'embedded-receiver.js' not in read('index.html'):
        raise ValueError('UI does not launch the bundled receiver')
    if not read('js/embedded-receiver.js'):
        raise ValueError('Missing receiver bootstrap')
