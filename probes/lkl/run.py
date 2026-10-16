#!/usr/bin/env python3
"""Serialize capability payloads; risky groups require a fresh externally coordinated window.
Usage: python3 probes/lkl/run.py --ack-window execmem execmem
The acknowledgement means Ps5Backend AND VulkanClient have released the console.
Never run this concurrently with another capability runner.
"""
import argparse
import fcntl
import json
from pathlib import Path
import re
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[2]
GROUPS = 'execmem backing process threads umtx faults cpu ptrace limits network storage introspection abi'.split()
RISKY = set('execmem backing process threads umtx faults cpu ptrace limits abi'.split())
CTL = ROOT / 'tools/ps5ctl'
OUT = ROOT / 'build/ps5/evidence/lkl'


def ctl(*args, **kwargs):
    return subprocess.run([str(CTL), *map(str, args)], cwd=ROOT, timeout=40, **kwargs)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('groups', nargs='+', choices=GROUPS)
    parser.add_argument('--ack-window', choices=GROUPS,
                        help='fresh idle acknowledgement for exactly one risky group')
    parser.add_argument('--no-build', action='store_true')
    args = parser.parse_args()
    risky = set(args.groups) & RISKY
    if risky and (len(args.groups) != 1 or args.ack_window != args.groups[0]):
        parser.error('coordinate a fresh idle window, then select ONE risky group with --ack-window GROUP')
    OUT.mkdir(parents=True, exist_ok=True)
    lock = (OUT / '.runner.lock').open('w')
    fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
    if not args.no_build:
        subprocess.run([str(ROOT / 'env/run'), 'make', '-C', 'probes/lkl'], cwd=ROOT, check=True)
    # Helper must exist for file: URI launch; upload does not start it.
    ctl('upload', ROOT / 'build/ps5/lklcap/ps5wl-lklcap-helper.elf', check=True)
    for group in args.groups:
        stamp = time.strftime('%Y%m%dT%H%M%S')
        base = OUT / f'{stamp}-{group}'
        with base.with_suffix('.processes-before.json').open('w') as out:
            health = ctl('ps', stdout=out, stderr=subprocess.STDOUT)
        if health.returncode:
            raise SystemExit('Console ps failed: STOP all console actions; inspect last saved logs')
        port = 9600 + GROUPS.index(group)
        with base.with_suffix('.log').open('w') as logs, base.with_suffix('.deploy.log').open('w') as deploy:
            pull = subprocess.Popen([str(CTL), 'logs', '--port', str(port), '--retry', '20'],
                                    cwd=ROOT, stdout=logs, stderr=subprocess.STDOUT)
            try:
                result = ctl('run', ROOT / f'build/ps5/lklcap/ps5wl-lklcap-{group}.elf',
                             stdout=deploy, stderr=subprocess.STDOUT)
                if result.returncode:
                    raise RuntimeError(f'deploy failed for {group}; see {deploy.name}')
                try:
                    pull.wait(timeout=60)
                except subprocess.TimeoutExpired:
                    pull.terminate()
                    pull.wait(timeout=5)
                    raise RuntimeError(f'log deadline for {group}; inspect own payload before any further runs')
            finally:
                if pull.poll() is None:
                    pull.terminate()
                    pull.wait(timeout=5)
        text = base.with_suffix('.log').read_text()
        exits = [line for line in text.splitlines() if re.search(r'(?:^|: )EXIT [0-9]+(?: |$)', line)]
        record = {'group': group, 'port': port, 'log': base.with_suffix('.log').name,
                  'deploy_status': result.returncode, 'log_status': pull.returncode,
                  'exit_lines': exits, 'exit_code': int(re.search(r'EXIT ([0-9]+)', exits[-1]).group(1)) if exits else None,
                  'watchdog_seconds': 55}
        base.with_suffix('.json').write_text(json.dumps(record, indent=2) + '\n')
        with base.with_suffix('.processes-after.json').open('w') as out:
            health = ctl('ps', stdout=out, stderr=subprocess.STDOUT)
        print(json.dumps(record), flush=True)
        if health.returncode:
            raise SystemExit('Console ps failed: STOP all console actions; report last saved logs')
        if not exits:
            raise SystemExit('No explicit EXIT line: UNTESTED/incomplete, not a capability failure; stop and investigate')
    return 0


if __name__ == '__main__':
    sys.exit(main())
