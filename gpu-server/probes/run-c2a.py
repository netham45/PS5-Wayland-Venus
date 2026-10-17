#!/usr/bin/env python3
"""Run ONE Backend-authorized producer/verifier pair, never the compositor."""
import argparse
from pathlib import Path
import queue
import subprocess
import threading

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'build/ps5/gpu-probes'
CTL = ROOT / 'tools/ps5ctl'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('bytes', type=int, choices=(16384, 8 << 20, 64 << 20))
    args = parser.parse_args()
    # Permission is external: running this command is itself the granted action.
    producer = f'ps5wl-gpu-c2a-{args.bytes}.elf'
    verifier = 'ps5wl-gpu-c2-verify.elf'
    path = OUT / f'c2a-{args.bytes}.log'
    lines = queue.Queue()
    lock = threading.Lock()
    with path.open('w') as log:
        def record(text):
            with lock:
                print(text, end='', flush=True)
                log.write(text)
                log.flush()

        def command(arguments, timeout=40):
            record('+ ' + ' '.join(map(str, arguments)) + '\n')
            result = subprocess.run(list(map(str, arguments)), stdout=subprocess.PIPE,
                                    stderr=subprocess.STDOUT, text=True, timeout=timeout)
            record(result.stdout)
            record(f'HOST command returncode={result.returncode}\n')
            if result.returncode:
                raise RuntimeError(f'command failed: {arguments}')
            return result.stdout

        command([CTL, 'upload', OUT / verifier])
        command([CTL, 'upload', OUT / producer])
        command([CTL, 'launch', producer])
        reader = subprocess.Popen(['timeout', '45', str(CTL), 'logs', '--port', '9593', '--retry', '10'],
                                  stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)

        def consume():
            for line in reader.stdout:
                record(line)
                lines.put(line)
            lines.put(None)

        thread = threading.Thread(target=consume)
        thread.start()
        ready = False
        producer_pass = False
        try:
            while not ready:
                line = lines.get(timeout=15)
                if line is None or 'STOP' in line:
                    raise RuntimeError('producer stopped before verifier readiness')
                ready = 'READY second-process verifier' in line
            # This dependent launch is part of the exact pre-authorized pair.
            command([CTL, 'launch', verifier])
            verify_log = command(['timeout', '10', CTL, 'logs', '--port', '9594', '--retry', '5'], timeout=15)
            if 'C2 cross-process RESULT PASS code=0' not in verify_log:
                raise RuntimeError('second process did not report PASS')
            while True:
                line = lines.get(timeout=15)
                if line is None:
                    break
                producer_pass |= 'C2 RESULT PASS code=0 wedged=0' in line
            code = reader.wait(timeout=5)
            record(f'HOST producer log returncode={code}\n')
            if code or not producer_pass:
                raise RuntimeError('producer did not finish PASS with a clean log EOF')
            record('HOST C2a PAIR PASS\n')
        finally:
            # Only host observers are stopped here, never a console process.
            if reader.poll() is None:
                reader.terminate()
            reader.wait(timeout=5)
            thread.join(timeout=5)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
