"""An http server that answers badly on purpose.

Every instance answers one way and one way only, so a test hits exactly the path it is
about. The port is picked by the operating system, so two of these never clash and a
broker or a real server already on a well known port is left alone.
"""

import socketserver
import threading
import time
from pathlib import Path
from urllib.parse import unquote


class _Handler(socketserver.StreamRequestHandler):

    @property
    def mock(self):
        return self.server.mock

    def handle(self):
        try:
            target = self.read_request()
            if target is None:
                return
            getattr(self, "do_" + self.mock.mode)(target)
        except OSError:
            pass

    def read_request(self):
        """Reads the request line and the headers, returns the target."""
        line = self.rfile.readline(65536)
        if not line:
            return None
        parts = line.decode("latin-1").split()
        while True:
            header = self.rfile.readline(65536)
            if header in (b"\r\n", b"\n", b""):
                break
        return parts[1] if len(parts) > 1 else "/"

    def resolve(self, target):
        """Turns the target into a path under the served directory."""
        name = unquote(target.split("?", 1)[0].split("#", 1)[0]).lstrip("/")
        root = Path(self.mock.directory).resolve()
        full = (root / name).resolve()
        if full != root and root not in full.parents:
            return None
        return full

    def send_head(self, status, reason, length, extra=""):
        head = (
            f"HTTP/1.1 {status} {reason}\r\n"
            f"Content-Length: {length}\r\n"
            "Connection: close\r\n"
            f"{extra}"
            "\r\n"
        )
        self.wfile.write(head.encode("latin-1"))

    def send_not_found(self):
        body = b"not found\n"
        self.send_head(404, "Not Found", len(body))
        self.wfile.write(body)

    def do_serve(self, target):
        path = self.resolve(target)
        if path is None or not path.is_file():
            self.send_not_found()
            return
        body = path.read_bytes()
        self.send_head(200, "OK", len(body), "Content-Type: application/json\r\n")
        self.wfile.write(body)

    def do_stall(self, target):
        deadline = time.monotonic() + self.mock.seconds
        while time.monotonic() < deadline and not self.mock.stopping:
            time.sleep(0.05)

    def do_slow(self, target):
        path = self.resolve(target)
        if path is None or not path.is_file():
            self.send_not_found()
            return
        body = path.read_bytes()
        self.send_head(200, "OK", len(body), "Content-Type: application/json\r\n")
        for start in range(0, len(body), self.mock.chunk):
            if self.mock.stopping:
                return
            self.wfile.write(body[start:start + self.mock.chunk])
            self.wfile.flush()
            time.sleep(self.mock.delay)

    def do_status(self, target):
        body = b"no\n"
        self.send_head(self.mock.code, self.mock.reason, len(body))
        self.wfile.write(body)

    def do_big(self, target):
        self.send_head(200, "OK", self.mock.size, "Content-Type: application/json\r\n")
        chunk = b"x" * 65536
        left = self.mock.size
        while left > 0:
            self.wfile.write(chunk[:min(left, len(chunk))])
            left -= len(chunk)

    def do_redirect(self, target):
        self.send_head(302, "Found", 0, f"Location: {self.mock.location}\r\n")


class _Server(socketserver.ThreadingTCPServer):
    allow_reuse_address = True
    daemon_threads = True


class HttpMock:
    """One http server in one mode. Use it as a context manager or call start and stop."""

    MODES = ("serve", "stall", "slow", "status", "big", "redirect")

    def __init__(
        self, mode="serve", directory=None, seconds=30.0, chunk=16, delay=1.0,
        code=500, reason="Internal Server Error", size=70000000, location="/elsewhere.json",
    ):
        if mode not in self.MODES:
            raise ValueError(f"unknown mode: {mode}")
        self.mode = mode
        self.directory = directory
        self.seconds = seconds
        self.chunk = chunk
        self.delay = delay
        self.code = code
        self.reason = reason
        self.size = size
        self.location = location
        self.stopping = False

        self.server = _Server(("127.0.0.1", 0), _Handler)
        self.server.mock = self
        self.port = self.server.server_address[1]
        self.thread = threading.Thread(target=self.server.serve_forever, daemon=True)

    @property
    def host(self):
        return f"127.0.0.1:{self.port}"

    def url(self, name):
        return f"http://{self.host}/{name}"

    def start(self):
        self.thread.start()
        return self

    def stop(self):
        self.stopping = True
        self.server.shutdown()
        self.server.server_close()

    def __enter__(self):
        return self.start()

    def __exit__(self, *args):
        self.stop()


def free_port():
    """A port nothing is listening on, for the case where the connect has to fail."""
    with socketserver.TCPServer(("127.0.0.1", 0), None, bind_and_activate=True) as s:
        return s.server_address[1]
