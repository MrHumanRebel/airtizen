#!/usr/bin/env python3
"""Ship build inputs and notices alongside the statically linked receiver."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tarfile

ROOT = Path(__file__).resolve().parents[1]
REVISION = '3a0867c2bfda4a4d4309ca1a8cbdc6175e67f587'


def bundle(destination):
    cache = Path(os.environ.get('AIRTIZEN_DEPS_DIR', ROOT.parent / '.airtizen-deps'))
    sdk = Path(os.environ.get('NACL_SDK_ROOT', cache / 'nacl-sdk/pepper_47'))
    destination = Path(destination)
    destination.mkdir(parents=True, exist_ok=True)
    subprocess.run(['git', 'archive', '--format=tar.gz', '--prefix=airtizen/',
                    '-o', str(destination.resolve() / 'AirTizen-source.tar.gz'), 'HEAD'], cwd=ROOT, check=True)
    shutil.copy2(cache / 'ffmpeg-7.1.5.tar.gz', destination / 'FFmpeg-source.tar.gz')
    # Includes the libraries' individual source/license headers and public SDK headers.
    with tarfile.open(destination / 'NaCl-runtime-source.tar.gz', 'w:gz') as archive:
        for name in ('src', 'include', 'LICENSE', 'COPYING', 'NOTICE'):
            archive.add(sdk / name, arcname='pepper_47/' + name)
    (destination / 'SOURCE-README.txt').write_text(
        'AirTizen-source.tar.gz is this release commit, including donor sources and build scripts.\n'
        'FFmpeg-source.tar.gz: upstream commit ' + REVISION + '.\n'
        'NaCl-runtime-source.tar.gz: Samsung Pepper 47 runtime sources, headers and notices.\n'
        'Rebuild with scripts/build-nacl-receiver.sh and scripts/stage-web.py --receiver.\n'
        'The pinned official SDK/toolchain is retrieved by scripts/install-nacl-sdk.sh.\n'
        'Production receiver uses FFmpeg AAC/ALAC; FDK is only a host test encoder.\n'
        'See THIRD_PARTY_NOTICES.md; the combined native binary is not MIT-only.\n')


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('destination', type=Path)
    bundle(parser.parse_args().destination)
