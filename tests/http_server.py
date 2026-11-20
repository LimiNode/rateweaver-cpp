"""Run native HTTP integration tests against an isolated loopback feed server."""

import argparse
import collections
import http.server
import pathlib
import select
import socket
import subprocess
import threading
import urllib.parse


class FeedServer(http.server.ThreadingHTTPServer):
    daemon_threads = True
    request_queue_size = 64

    def __init__(self):
        super().__init__(("127.0.0.1", 0), Handler)
        self.counts = collections.Counter()
        self.started = collections.defaultdict(threading.Event)
        self.release = collections.defaultdict(threading.Event)
        self.disconnected = collections.defaultdict(threading.Event)
        self.lock = threading.Lock()


class Handler(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def handle(self):
        try:
            super().handle()
        except (BrokenPipeError, ConnectionResetError, ConnectionAbortedError):
            # Native cancellation can also close an idle keepalive connection
            # while BaseHTTPRequestHandler is reading the next request line.
            pass

    def log_message(self, *_):
        pass

    def respond(self, status, body=b"", headers=None):
        self.send_response(status)
        self.send_header("Content-Length", str(len(body)))
        for name, value in (headers or {}).items():
            self.send_header(name, value)
        self.end_headers()
        if body:
            self.wfile.write(body)
            self.wfile.flush()

    def do_GET(self):
        path = urllib.parse.urlsplit(self.path).path
        with self.server.lock:
            self.server.counts[path] += 1
            count = self.server.counts[path]
        try:
            if path.startswith("/state/"):
                self.respond(200, b"1" if self.server.started[path[7:]].is_set() else b"0")
                return
            if path.startswith("/release/"):
                self.server.release[path[9:]].set()
                self.respond(200, b"1")
                return
            if path.startswith("/disconnected/"):
                self.respond(200, b"1" if self.server.disconnected[path[14:]].is_set() else b"0")
                return
            if path.startswith("/count/"):
                self.respond(200, str(self.server.counts["/" + path[7:]]).encode())
                return
            if path.startswith("/gate/"):
                name = path[6:]
                self.server.started[name].set()
                if self.wait_gate(name):
                    self.respond(200, CDN)
                return
            if path.startswith("/stream/"):
                name = path[8:]
                self.send_response(200)
                self.send_header("Content-Length", str(1024 * 1024))
                self.end_headers()
                self.wfile.write(b"x" * 4096)
                self.wfile.flush()
                self.server.started[name].set()
                if self.wait_gate(name):
                    self.wfile.write(b"x" * (1024 * 1024 - 4096))
                return
            if path == "/chunked":
                self.send_response(200)
                self.send_header("Transfer-Encoding", "chunked")
                self.end_headers()
                for offset in range(0, len(CDN), 7):
                    chunk = CDN[offset:offset + 7]
                    self.wfile.write(("%x\r\n" % len(chunk)).encode() + chunk + b"\r\n")
                    self.wfile.flush()
                self.wfile.write(b"0\r\n\r\n")
                self.wfile.flush()
                return
            if path == "/etag":
                if self.headers.get("If-None-Match") == '"stable-v1"':
                    self.respond(304)
                else:
                    self.respond(200, CDN, {"ETag": '"stable-v1"'})
                return
            if path == "/unsolicited304":
                self.respond(304)
                return
            if path == "/retry" and count == 1:
                # An empty failed attempt is retryable even with native streaming.
                self.respond(500)
                return
            if path == "/headers" and self.headers.get("X-Host-Setting") != "preserved":
                self.respond(400, b"host header lost")
                return
            if path == "/shared-second" and not self.server.release["shared-limit"].is_set():
                self.respond(409, b"shared native limiter admitted overlapping request")
                return
            bodies = {"/cdn": CDN, "/retry": CDN, "/headers": CDN,
                      "/ownedlimit": CDN, "/shared-second": CDN,
                      "/cbr": CBR, "/ecb": ECB,
                      "/malformed": b"{", "/malformedxml": b"<broken", "/rawempty": b"",
                      "/empty": b'{"date":"2024-01-02","usd":{}}',
                      "/partial": b'{"date":"2024-01-02","usd":{"eur":0.8,"jpy":-1}}'}
            if path in bodies:
                self.respond(200, bodies[path])
            elif path == "/error":
                self.respond(503, b"upstream unavailable")
            elif path == "/error500":
                self.respond(500)
            else:
                self.respond(404)
        except (BrokenPipeError, ConnectionResetError, ConnectionAbortedError):
            # Cancellation and timeout intentionally close the peer connection.
            pass

    def wait_gate(self, name):
        # Observe real peer EOF; a publication fence alone must not satisfy
        # the tests that require cancellation of the native TCP operation.
        for _ in range(1500):
            if self.server.release[name].wait(0.01):
                return True
            try:
                readable, _, _ = select.select([self.connection], [], [], 0)
                if readable and not self.connection.recv(1, socket.MSG_PEEK):
                    self.server.disconnected[name].set()
                    return False
            except OSError:
                self.server.disconnected[name].set()
                return False
        return False


CDN = b'{"date":"2024-01-02","usd":{"usd":1,"eur":0.8,"jpy":120}}'
CBR = b'{"Date":"2024-01-02T11:30:00+03:00","Valute":{"USD":{"CharCode":"USD","Nominal":1,"Value":90}}}'
ECB = b'<gesmes:Envelope xmlns:gesmes="http://www.gesmes.org/xml/2002-08-01" xmlns="http://www.ecb.int/vocabulary/2002-08-01/eurofxref"><Cube><Cube time="2024-01-02"><Cube currency="USD" rate="1.25"/></Cube></Cube></gesmes:Envelope>'


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--exe", type=pathlib.Path, required=True)
    parser.add_argument("--timeout", type=int, default=90)
    parser.add_argument("--manual", action="store_true")
    parser.add_argument("--url-path", default="")
    arguments = parser.parse_args()
    with FeedServer() as server:
        worker = threading.Thread(target=server.serve_forever, daemon=True)
        worker.start()
        try:
            command = [str(arguments.exe.resolve()),
                       "http://127.0.0.1:" + str(server.server_port) + arguments.url_path]
            if arguments.manual:
                command.append("manual")
            result = subprocess.run(command,
                                    timeout=arguments.timeout, check=False)
            return result.returncode
        finally:
            for event in list(server.release.values()):
                event.set()
            server.shutdown()
            worker.join(timeout=5)


if __name__ == "__main__":
    raise SystemExit(main())
