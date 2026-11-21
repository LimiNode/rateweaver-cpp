"""Verify TLS and runtime deployment against a local, explicitly trusted fixture."""

import argparse
import http.server
import os
import pathlib
import ssl
import subprocess
import threading


class Handler(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        self.send_response(200)
        self.send_header("Content-Length", "2")
        self.end_headers()
        self.wfile.write(b"ok")

    def log_message(self, *_):
        pass


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--exe", type=pathlib.Path, required=True)
    arguments = parser.parse_args()
    fixtures = pathlib.Path(__file__).resolve().parent / "fixtures" / "tls"
    context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    context.load_cert_chain(fixtures / "localhost.pem", fixtures / "localhost-key.pem")
    environment = os.environ.copy()
    if os.name == "nt":
        environment["PATH"] = str(pathlib.Path(os.environ["SystemRoot"]) / "System32")
    with http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler) as server:
        server.socket = context.wrap_socket(server.socket, server_side=True)
        worker = threading.Thread(target=server.serve_forever, daemon=True)
        worker.start()
        try:
            return subprocess.run(
                [str(arguments.exe.resolve()), f"https://localhost:{server.server_port}/",
                 str(fixtures / "localhost.pem")],
                env=environment, timeout=30, check=False).returncode
        finally:
            server.shutdown()
            worker.join(timeout=5)


if __name__ == "__main__":
    raise SystemExit(main())
