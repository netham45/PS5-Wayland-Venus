#!/usr/bin/env python3
"""Build the in-process vkr-only GPU server in the pinned env/run container."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tarfile
import urllib.request

ROOT = Path(__file__).resolve().parents[1]
BASE = ROOT / 'build/gpud'

def run(*args):
    subprocess.run(list(map(str, args)), check=True)

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('target', choices=['host', 'ps5'])
    p.add_argument('--sanitize', action='store_true')
    a = p.parse_args()
    if os.environ.get('PS5WL_BUILD_ENV') != '1':
        p.error('run through env/run')
    pin = next(x for x in json.loads((ROOT/'deps/versions.lock.json').read_text())['dependencies'] if x['name']=='virglrenderer')
    archive = ROOT/'.cache/deps'/pin['archive']
    if not archive.exists():
        archive.parent.mkdir(parents=True, exist_ok=True)
        with urllib.request.urlopen(pin['url'], timeout=120) as response, archive.open('wb') as output:
            shutil.copyfileobj(response, output)
    if hashlib.sha256(archive.read_bytes()).hexdigest() != pin['sha256']:
        raise RuntimeError('virglrenderer archive checksum mismatch')
    src = BASE/'sources'/pin['source_dir']
    patches = sorted((ROOT/'gpu-server/core/patches').glob('*.patch'))
    digest = hashlib.sha256(b''.join(x.read_bytes() for x in patches)).hexdigest()
    stamp = src/'.ps5wl-patches'
    if src.exists() and (not stamp.exists() or stamp.read_text() != digest):
        shutil.rmtree(src)
    if not src.exists():
        src.parent.mkdir(parents=True, exist_ok=True)
        with tarfile.open(archive) as t:
            t.extractall(src.parent, filter='data')
        for patch in patches:
            subprocess.run(['patch','-p1','-i',str(patch)], cwd=src, check=True)
        stamp.write_text(digest)
    build = BASE/(a.target + ('-sanitize' if a.sanitize else ''))
    prefix = ROOT/'build/ps5/sysroot' if a.target == 'ps5' else build/'install'
    options = ['-Dvkr_source='+str(src), '--prefix='+str(prefix)]
    if a.sanitize:
        options += ['-Db_sanitize=address,undefined','-Db_lundef=false']
    if a.target == 'ps5':
        run(ROOT/'cross/bootstrap-compat.sh')
        options += ['--cross-file='+str(ROOT/'build/ps5/cross.ini')]
    run('meson','setup',build,ROOT/'gpu-server',*(['--reconfigure'] if (build/'build.ninja').exists() else []),*options)
    run('meson','compile','-C',build,'-j',os.environ.get('JOBS','8'))
    run('meson','install','-C',build,'--no-rebuild')

if __name__ == '__main__':
    main()
