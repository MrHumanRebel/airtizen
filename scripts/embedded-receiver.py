"""Check the shipped receiver's files/architecture, independent of the SDK loader."""
import json
from pathlib import PurePosixPath
import struct

REQUIRED_SYMBOLS = frozenset({
    'PPP_InitializeModule', 'PPP_GetInterface', 'PPP_ShutdownModule',
    'PpapiPluginMain', 'main', 'airtizen_runtime_start',
    'airtizen_aac_create', 'airtizen_alac_create',
    'ff_aac_decoder', 'ff_alac_decoder', 'ff_h264_decoder',
    'airtizen_nacl_udp_multicast_v1',
})


def check_runtime_symbols(data):
    """Inspect defined ELF symbols without requiring the build SDK at signing."""
    offset = struct.unpack_from('<I', data, 32)[0]
    size, count = struct.unpack_from('<HH', data, 46)
    if size != 40 or not count or offset + size * count > len(data):
        raise ValueError('Receiver must retain its auditable ELF symbol table')
    sections = [struct.unpack_from('<IIIIIIIIII', data, offset + size * i) for i in range(count)]
    defined = set()
    for section in sections:
        if section[1] != 2:  # SHT_SYMTAB
            continue
        start, length, link, entry_size = section[4], section[5], section[6], section[9]
        if entry_size != 16 or length % entry_size or start + length > len(data) or link >= count:
            raise ValueError('Invalid receiver symbol table')
        strings = sections[link]
        if strings[1] != 3 or strings[4] + strings[5] > len(data):
            raise ValueError('Invalid receiver symbol strings')
        names = data[strings[4]:strings[4] + strings[5]]
        for index in range(start, start + length, entry_size):
            name, value, symbol_size, info, other, section_index = struct.unpack_from('<IIIBBH', data, index)
            if name >= len(names):
                raise ValueError('Invalid receiver symbol name')
            end = names.find(b'\0', name)
            if end < 0:
                raise ValueError('Unterminated receiver symbol name')
            if section_index:
                defined.add(names[name:end].decode('ascii', errors='replace'))
    missing = REQUIRED_SYMBOLS - defined
    if missing:
        raise ValueError('Missing bundled PPAPI receiver/codec symbols: ' + ', '.join(sorted(missing)))


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
        if segment in (2, 3):  # PT_DYNAMIC / PT_INTERP
            raise ValueError('Receiver must be statically linked, without external shared libraries')
        if segment == 1:
            if files > memory or start + files > len(data):
                raise ValueError('Truncated receiver ELF segment')
            if flags & 1 and address <= entry < address + memory:
                executable = True
    if not executable:
        raise ValueError('Receiver entry point is not in an executable segment')
    check_runtime_symbols(data)
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
