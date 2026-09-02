import argparse
import http.server
import json
import os
import pathlib
import subprocess
import threading
import time
import urllib.parse


class HangingMetroHandler(http.server.BaseHTTPRequestHandler):
    def send_body(self, status, body):
        self.send_response(status)
        self.send_header("Content-Type", "text/plain")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        path = urllib.parse.urlsplit(self.path).path
        if path == "/status":
            self.send_body(200, b"packager-status:running")
            self.server.status_succeeded.set()
            return

        if path == "/index.bundle":
            self.send_response(200)
            self.send_header("Content-Type", "application/javascript")
            self.send_header("Content-Length", "4096")
            self.end_headers()
            self.wfile.write(b"/* deliberately incomplete")
            self.wfile.flush()
            self.server.bundle_started_at = time.monotonic()
            self.server.bundle_started.set()
            self.server.release_response.wait(timeout=5)
            return

        self.send_body(404, b"")

    def log_message(self, format, *args):
        del format, args


def require(condition, message):
    if not condition:
        raise AssertionError(message)


def stop_process(process):
    if process.poll() is not None:
        return
    process.terminate()
    try:
        process.wait(timeout=2)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait(timeout=2)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--rnsim", required=True)
    parser.add_argument("--output", required=True)
    args = parser.parse_args()

    output_path = pathlib.Path(args.output)
    output_path.unlink(missing_ok=True)
    server = http.server.ThreadingHTTPServer(
        ("127.0.0.1", 0), HangingMetroHandler
    )
    server.status_succeeded = threading.Event()
    server.bundle_started = threading.Event()
    server.bundle_started_at = None
    server.release_response = threading.Event()
    server_thread = threading.Thread(target=server.serve_forever, daemon=True)
    server_thread.start()

    url = (
        f"http://127.0.0.1:{server.server_port}/index.bundle"
        "?platform=android&dev=true&minify=false"
    )
    environment = os.environ.copy()
    environment["RNS_INTERACTIVE_SMOKE_OUTPUT"] = str(output_path)
    environment["RNS_INTERACTIVE_SMOKE_TIMEOUT_MS"] = "200"
    process = subprocess.Popen(
        [
            args.rnsim,
            "interactive",
            "--url",
            url,
            "--app-key",
            "CancellationSmoke",
        ],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        env=environment,
    )

    try:
        require(
            server.status_succeeded.wait(timeout=3),
            "interactive preparation never received a successful Metro status",
        )
        require(
            server.bundle_started.wait(timeout=3),
            "interactive preparation never started the hanging bundle response",
        )
        try:
            stdout, stderr = process.communicate(timeout=3)
        except subprocess.TimeoutExpired as error:
            raise AssertionError(
                "closing the smoke window did not cancel the hanging bundle fetch"
            ) from error

        cancellation_elapsed = time.monotonic() - server.bundle_started_at
        require(
            cancellation_elapsed < 1.0,
            f"hanging bundle cancellation took {cancellation_elapsed:.3f}s",
        )
        require(
            process.returncode == 0,
            f"cancelled interactive preparation exited {process.returncode}: "
            f"{stderr}{stdout}",
        )
        require(output_path.is_file(), "interactive frontend produced no smoke JSON")
        report = json.loads(output_path.read_text())
        require(report.get("window") is True, report)
        require(report.get("ready") is False, report)
    finally:
        server.release_response.set()
        stop_process(process)
        server.shutdown()
        server.server_close()
        server_thread.join(timeout=2)

    print(
        "interactive close cancelled a hanging Metro bundle response in "
        f"{cancellation_elapsed:.3f}s"
    )


if __name__ == "__main__":
    main()
