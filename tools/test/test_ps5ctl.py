#!/usr/bin/env python3
"""Exercise ps5ctl's real TCP screenshot receiver using a complete 1x1 PNG."""
import os
from pathlib import Path
import socket
import struct
import subprocess
import tempfile
import threading
import unittest
import zlib

ROOT = Path(__file__).resolve().parents[2]


def chunk(kind, data):
    return struct.pack("!I", len(data)) + kind + data + struct.pack("!I", zlib.crc32(kind + data))


PNG = (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack("!IIBBBBB", 1, 1, 8, 2, 0, 0, 0))
       + chunk(b"IDAT", zlib.compress(b"\0\xff\0\0")) + chunk(b"IEND", b""))


class ScreenshotTest(unittest.TestCase):
    def transfer(self, data, existing=None):
        (ROOT / "build").mkdir(exist_ok=True)
        with tempfile.TemporaryDirectory(dir=ROOT / "build") as directory:
            output = Path(directory) / "shot.png"
            if existing is not None:
                output.write_bytes(existing)
            with socket.socket() as listener:
                listener.bind(("127.0.0.1", 0))
                listener.listen(1)
                port = listener.getsockname()[1]

                def serve():
                    with listener.accept()[0] as client:
                        for offset in range(0, len(data), 3):
                            client.sendall(data[offset:offset + 3])

                thread = threading.Thread(target=serve, daemon=True)
                thread.start()
                environment = dict(os.environ, PS5_HOST="127.0.0.1")
                result = subprocess.run([str(ROOT / "tools/ps5ctl"), "screenshot", "--port", str(port),
                                         "--output", str(output), "--retry", "0"], env=environment,
                                        capture_output=True, text=True, timeout=5)
                thread.join(timeout=2)
                self.assertFalse(thread.is_alive())
                actual = output.read_bytes() if output.exists() else None
                self.assertEqual(sorted(path.name for path in Path(directory).iterdir()),
                                 ["shot.png"] if actual is not None else [])
                return result, actual

    def test_fragmented_complete_png(self):
        result, actual = self.transfer(PNG)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(actual, PNG)

    def test_truncated_png_preserves_destination(self):
        result, actual = self.transfer(PNG[:-12], existing=b"previous image")
        self.assertEqual(result.returncode, 1)
        self.assertIn("closed before PNG IEND", result.stderr)
        self.assertEqual(actual, b"previous image")

    def test_log_port_is_not_a_screenshot(self):
        result, actual = self.transfer(b"not PNG\n")
        self.assertEqual(result.returncode, 1)
        self.assertIn("invalid PNG signature", result.stderr)
        self.assertIsNone(actual)


if __name__ == "__main__":
    unittest.main(verbosity=2)
