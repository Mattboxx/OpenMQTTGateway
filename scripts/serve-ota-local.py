"""Serve only a selected ESP32 application, paced for a low-memory gateway.

Temporary, unauthenticated LAN helper; bind to the PC's LAN address, never an
internet-facing interface. Stop after the device confirms its new version.
"""
import argparse
import hashlib
import http.server
import pathlib
import socket
import struct
import sys
import time

parser = argparse.ArgumentParser()
parser.add_argument("firmware", type=pathlib.Path)
parser.add_argument("--bind", required=True)
parser.add_argument("--port", type=int, default=8010)
parser.add_argument("--interface-index", type=int,
                    help="Windows LAN interface index; pin responses without changing VPN/routes")
parser.add_argument("--delay-ms", type=int, default=45,
                    help="Pause between blocks (1..1000 ms)")
parser.add_argument("--chunk-size", type=int, choices=(256, 512, 1024), default=1024)
parser.add_argument("--initial-delay-ms", type=int, default=0,
                    help="Pause after headers before the first image block (0..5000 ms)")
parser.add_argument("--lifetime-minutes", type=int, default=20,
                    help="Stop the temporary endpoint after this period (1..60 minutes)")
args = parser.parse_args()
if args.interface_index is not None:
    if sys.platform != "win32" or not 1 <= args.interface_index <= 0xFFFFFF:
        parser.error("--interface-index requires Windows and an index between 1 and 16777215")
if not 1 <= args.delay_ms <= 1000:
    parser.error("--delay-ms must be between 1 and 1000")
if not 0 <= args.initial_delay_ms <= 5000:
    parser.error("--initial-delay-ms must be between 0 and 5000")
if not 1 <= args.lifetime_minutes <= 60:
    parser.error("--lifetime-minutes must be between 1 and 60")
image = args.firmware.resolve(strict=True)
if not image.is_file() or image.suffix.lower() != ".bin":
    parser.error("Select an application .bin file")
digest = hashlib.md5()
with image.open("rb") as source:
    if source.read(1) != b"\xe9":
        parser.error("Not an ESP application image")
    source.seek(0)
    for block in iter(lambda: source.read(65536), b""):
        digest.update(block)


class FirmwareOnlyHandler(http.server.BaseHTTPRequestHandler):
    def setup(self):
        super().setup()
        if args.interface_index is not None:
            # IP_UNICAST_IF takes a DWORD in network byte order. Binding the
            # listening address alone does not pin replies on multihomed PCs.
            self.connection.setsockopt(socket.IPPROTO_IP, 31,
                                       struct.pack("!I", args.interface_index))
        self.connection.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        self.connection.settimeout(10)

    def serve_image(self, include_body):
        if self.path != "/firmware.bin":
            self.send_error(404)
            return
        self.send_response(200)
        self.send_header("Content-Type", "application/octet-stream")
        self.send_header("Content-Length", str(image.stat().st_size))
        self.send_header("x-MD5", digest.hexdigest())
        self.send_header("Connection", "close")
        self.end_headers()
        if not include_body:
            return
        transferred = 0
        next_progress = 256 * 1024
        started = time.monotonic()
        try:
            time.sleep(args.initial_delay_ms / 1000)
            with image.open("rb") as source:
                for block in iter(lambda: source.read(args.chunk_size), b""):
                    if time.monotonic() - started > 2400:
                        raise TimeoutError("Transfer deadline exceeded")
                    self.wfile.write(block)
                    transferred += len(block)
                    if transferred >= next_progress:
                        print(f"Transfer progress: {transferred} bytes", flush=True)
                        next_progress += 256 * 1024
                    time.sleep(args.delay_ms / 1000)
            print(f"Transfer complete: {transferred} bytes", flush=True)
        except (OSError, TimeoutError) as error:
            print(f"Transfer stopped at {transferred} bytes: {error}", flush=True)

    def do_GET(self):
        self.serve_image(True)

    def do_HEAD(self):
        self.serve_image(False)


# Browsers can preconnect without sending a request. One silent connection
# must not block a different phone/ESP request behind a 45-second read timeout.
server = http.server.ThreadingHTTPServer((args.bind, args.port), FirmwareOnlyHandler)
server.timeout = 1
deadline = time.monotonic() + args.lifetime_minutes * 60
print(f"Temporary OTA endpoint: http://{args.bind}:{args.port}/firmware.bin", flush=True)
try:
    while time.monotonic() < deadline:
        server.handle_request()
except KeyboardInterrupt:
    pass
finally:
    server.server_close()
