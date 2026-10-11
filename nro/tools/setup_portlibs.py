#!/usr/bin/env python3
"""Install official, hash-checked portlibs into the ignored local build cache.

Uses the existing devkitPro pacman metadata; never updates the toolchain or writes
into DEVKITPRO. System-wide installation with dkp-pacman remains supported.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tarfile
import urllib.request


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--prefix', type=Path, default=Path(__file__).resolve().parents[1] / 'build/portlibs')
    parser.add_argument('--cache', type=Path)
    args = parser.parse_args()
    devkit = Path(os.environ['DEVKITPRO'])
    db = devkit / 'pacman/var/lib/pacman/sync/dkp-libs.db'
    entries = {}
    with tarfile.open(db) as archive:
        for member in archive:
            if not member.name.endswith('/desc'):
                continue
            fields = {}
            for part in archive.extractfile(member).read().decode().strip().split('\n\n'):
                lines = part.splitlines()
                fields[lines[0].strip('%')] = lines[1:]
            entries[fields['FILENAME'][0]] = fields
    urls = subprocess.check_output(
        ['dkp-pacman', '-Sp', 'switch-glfw', 'switch-mesa', 'switch-glm'], text=True).splitlines()
    cache = args.cache or args.prefix.parent / 'portlibs-packages'
    cache.mkdir(parents=True, exist_ok=True)
    args.prefix.mkdir(parents=True, exist_ok=True)
    manifest = []
    for url in urls:
        name = url.rsplit('/', 1)[-1]
        fields = entries[name]
        expected = fields['SHA256SUM'][0]
        package = cache / name
        if not package.exists() or hashlib.sha256(package.read_bytes()).hexdigest() != expected:
            failures = []
            for candidate in (url + '?download=1', url.replace('pkg.devkitpro.org', 'downloads.devkitpro.org') + '?download=1'):
                try:
                    request = urllib.request.Request(candidate, headers={'User-Agent': 'pacman/6.0.1'})
                    with urllib.request.urlopen(request, timeout=60) as response:
                        data = response.read()
                    if hashlib.sha256(data).hexdigest() != expected:
                        raise RuntimeError('package SHA-256 does not match local devkitPro metadata')
                    package.write_bytes(data)
                    break
                except Exception as exc:
                    failures.append(str(exc))
            else:
                raise RuntimeError(name + ': ' + '; '.join(failures))
        # Verified devkitPro packages only; strip the system prefix into our cache.
        subprocess.run(['tar', '-xf', str(package.resolve()), '-C', str(args.prefix.resolve()),
                        '--strip-components=4', 'opt/devkitpro/portlibs/switch'], check=True)
        manifest.append({'package': fields['NAME'][0], 'version': fields['VERSION'][0], 'sha256': expected})
        print(name + ': verified and installed', flush=True)
    (args.prefix / 'packages.json').write_text(json.dumps(manifest, indent=2) + '\n')
    print('Local portlibs: ' + str(args.prefix.resolve()))


if __name__ == '__main__':
    main()
