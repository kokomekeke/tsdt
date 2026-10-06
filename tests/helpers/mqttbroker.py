"""A small MQTT 3.1.1 broker, enough for what MqttClient does.

It handles connect, subscribe, publish at all three quality of service levels and ping,
which is everything the server and the commander use. Running it inside the test process
means the tests need no broker installed and no container.

Everything a client publishes is kept with the time it arrived, so a test can ask for
what came back after the moment it sent its command.
"""

import json
import socketserver
import struct
import threading
import time


def encode_remaining(n):
    out = bytearray()
    while True:
        b = n % 128
        n //= 128
        if n:
            b |= 0x80
        out.append(b)
        if not n:
            return bytes(out)


def encode_string(s):
    b = s.encode()
    return struct.pack("!H", len(b)) + b


def topic_matches(filt, topic):
    f = filt.split("/")
    t = topic.split("/")
    for i, part in enumerate(f):
        if part == "#":
            return True
        if i >= len(t):
            return False
        if part != "+" and part != t[i]:
            return False
    return len(f) == len(t)


class _Handler(socketserver.StreamRequestHandler):

    @property
    def broker(self):
        return self.server.broker

    def setup(self):
        super().setup()
        self.wlock = threading.Lock()
        self.filters = []
        self.broker.add_client(self)

    def finish(self):
        self.broker.remove_client(self)
        try:
            super().finish()
        except OSError:
            pass

    def send_packet(self, data):
        with self.wlock:
            try:
                self.wfile.write(data)
                self.wfile.flush()
            except OSError:
                pass

    def read_packet(self):
        head = self.rfile.read(1)
        if not head:
            return None
        multiplier = 1
        length = 0
        while True:
            b = self.rfile.read(1)
            if not b:
                return None
            length += (b[0] & 127) * multiplier
            multiplier *= 128
            if not b[0] & 0x80:
                break
        body = self.rfile.read(length) if length else b""
        return head[0], body

    def handle(self):
        try:
            self.run()
        except OSError:
            pass

    def run(self):
        while True:
            packet = self.read_packet()
            if packet is None:
                return
            first, body = packet
            ptype = first >> 4
            flags = first & 0x0F

            if ptype == 1:  # connect
                self.send_packet(b"\x20\x02\x00\x00")
            elif ptype == 8:  # subscribe
                self.on_subscribe(body)
            elif ptype == 3:  # publish
                self.on_publish(flags, body)
            elif ptype == 6:  # pubrel
                pid = struct.unpack("!H", body[:2])[0]
                self.send_packet(b"\x70\x02" + struct.pack("!H", pid))
            elif ptype == 12:  # pingreq
                self.send_packet(b"\xd0\x00")
            elif ptype == 14:  # disconnect
                return

    def on_subscribe(self, body):
        pid = struct.unpack("!H", body[:2])[0]
        i = 2
        granted = []
        while i < len(body):
            length = struct.unpack("!H", body[i:i + 2])[0]
            i += 2
            self.filters.append(body[i:i + length].decode())
            i += length + 1
            granted.append(0)
        self.send_packet(
            b"\x90" + encode_remaining(2 + len(granted))
            + struct.pack("!H", pid) + bytes(granted)
        )

    def on_publish(self, flags, body):
        qos = (flags >> 1) & 3
        length = struct.unpack("!H", body[:2])[0]
        topic = body[2:2 + length].decode()
        i = 2 + length
        pid = None
        if qos > 0:
            pid = struct.unpack("!H", body[i:i + 2])[0]
            i += 2
        self.broker.record(topic, body[i:])
        if qos == 1:
            self.send_packet(b"\x40\x02" + struct.pack("!H", pid))
        elif qos == 2:
            self.send_packet(b"\x50\x02" + struct.pack("!H", pid))


class _Server(socketserver.ThreadingTCPServer):
    allow_reuse_address = True
    daemon_threads = True


class Broker:
    """Listens on a port the operating system picks and keeps every message it sees."""

    def __init__(self, host="127.0.0.1"):
        self.lock = threading.Lock()
        self.clients = []
        self.messages = []
        self.server = _Server((host, 0), _Handler)
        self.server.broker = self
        self.host, self.port = self.server.server_address[:2]
        self.thread = threading.Thread(target=self.server.serve_forever, daemon=True)

    def start(self):
        self.thread.start()
        return self

    def stop(self):
        self.server.shutdown()
        self.server.server_close()

    def __enter__(self):
        return self.start()

    def __exit__(self, *args):
        self.stop()

    def add_client(self, client):
        with self.lock:
            self.clients.append(client)

    def remove_client(self, client):
        with self.lock:
            if client in self.clients:
                self.clients.remove(client)

    def subscriber_count(self):
        with self.lock:
            return sum(1 for c in self.clients if c.filters)

    def mark(self):
        return time.monotonic()

    def record(self, topic, payload):
        with self.lock:
            self.messages.append((time.monotonic(), topic, payload.decode("utf-8", "replace")))
        self.route(topic, payload)

    def route(self, topic, payload):
        with self.lock:
            targets = [c for c in self.clients if any(topic_matches(f, topic) for f in c.filters)]
        packet = (
            b"\x30" + encode_remaining(2 + len(topic.encode()) + len(payload))
            + encode_string(topic) + payload
        )
        for client in targets:
            client.send_packet(packet)

    def publish(self, topic, payload):
        self.route(topic, payload.encode())

    def since(self, mark, topic=None):
        with self.lock:
            return [
                (ts, t, p) for (ts, t, p) in self.messages
                if ts > mark and (topic is None or t == topic)
            ]

    def wait_for_error(self, mark, topic, timeout=6.0):
        """Waits for a message carrying a reason. Returns (text, seconds) or None.

        A reason rides in the description of a signal status, there is no message of its
        own for one.
        """
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            for ts, _, payload in self.since(mark, topic):
                try:
                    body = json.loads(payload)
                except ValueError:
                    continue
                if not isinstance(body, dict):
                    continue
                reason = body.get("description", {}).get("error_message")
                if reason is not None:
                    return reason, ts - mark
            time.sleep(0.02)
        return None
