import argparse
import http.server
import json
import os
import pathlib
import subprocess
import threading
import urllib.parse


class RetryMetroHandler(http.server.BaseHTTPRequestHandler):
    def send_body(self, status, body, content_type="text/plain"):
        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        path = urllib.parse.urlsplit(self.path).path
        if path == "/status":
            self.send_body(200, b"packager-status:running")
            return

        if path == "/index.bundle":
            with self.server.request_lock:
                self.server.bundle_requests += 1
                request_number = self.server.bundle_requests
            if request_number == 1:
                self.send_body(503, b"deliberate first preparation failure")
                self.server.first_failure.set()
                return

            self.server.retry_seen.set()
            if not self.server.allow_success.wait(timeout=5):
                self.send_body(504, b"test did not release retry response")
                return
            self.send_body(
                200, self.server.bundle_bytes, "application/javascript"
            )
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
    parser.add_argument("--bundle", required=True)
    parser.add_argument("--addon", required=True)
    parser.add_argument("--output", required=True)
    parser.add_argument("--app-key")
    parser.add_argument("--timeout-ms", type=int, default=15000)
    args = parser.parse_args()

    output_path = pathlib.Path(args.output)
    output_path.unlink(missing_ok=True)
    bundle_path = pathlib.Path(args.bundle)
    bundle_bytes = bundle_path.read_bytes()

    server = http.server.ThreadingHTTPServer(
        ("127.0.0.1", 0), RetryMetroHandler
    )
    server.bundle_bytes = bundle_bytes
    server.bundle_requests = 0
    server.request_lock = threading.Lock()
    server.first_failure = threading.Event()
    server.retry_seen = threading.Event()
    server.allow_success = threading.Event()
    server_thread = threading.Thread(target=server.serve_forever, daemon=True)
    server_thread.start()

    url = (
        f"http://127.0.0.1:{server.server_port}/index.bundle"
        "?platform=android&dev=true&minify=false"
    )
    environment = os.environ.copy()
    environment["RNS_INTERACTIVE_SMOKE_OUTPUT"] = str(output_path)
    environment["RNS_INTERACTIVE_SMOKE_TIMEOUT_MS"] = str(args.timeout_ms)
    environment["RNS_INTERACTIVE_SMOKE_RETRY_PREPARATION"] = "1"
    command = [
        args.rnsim,
        "interactive",
        "--url",
        url,
        "--addon",
        args.addon,
    ]
    if args.app_key:
        command.extend(["--app-key", args.app_key])
    process = subprocess.Popen(
        command,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        env=environment,
    )

    try:
        require(
            server.first_failure.wait(timeout=5),
            "rnsim never reached the deliberate preparation failure",
        )
        require(
            server.retry_seen.wait(timeout=5),
            "rnsim did not retry preparation after the first failure",
        )
        require(
            process.poll() is None,
            "interactive window process exited before retry could succeed",
        )
        require(
            not output_path.exists(),
            "interactive smoke completed before the retry response was released",
        )

        server.allow_success.set()
        try:
            stdout, stderr = process.communicate(timeout=25)
        except subprocess.TimeoutExpired as error:
            raise AssertionError("rnsim did not finish after retry succeeded") from error

        require(
            process.returncode == 0,
            f"rnsim failed after preparation retry ({process.returncode}): "
            f"{stderr}{stdout}",
        )
        require(output_path.is_file(), "interactive frontend produced no smoke JSON")
        report = json.loads(output_path.read_text())
        require(report.get("window") is True, report)
        require(report.get("preparationFailures") == 1, report)
        require(report.get("preparationRetries") == 1, report)
        require(report.get("moduleOpens") == 1, report)
        require(report.get("moduleCreates") == 1, report)
        require(report.get("planFinalizations") == 1, report)
        require(report.get("planApplications") == 1, report)
        require(server.bundle_requests >= 2, server.bundle_requests)
        if args.app_key:
            require(report.get("ready") is True, report)
            require(report.get("capabilityUsages", 0) > 0, report)
            require(report.get("componentCapabilityUsages", 0) > 0, report)
            require(report.get("frameWidth", 0) > 0, report)
            require(report.get("frameHeight", 0) > 0, report)
    finally:
        server.allow_success.set()
        stop_process(process)
        server.shutdown()
        server.server_close()
        server_thread.join(timeout=2)

    print(
        "interactive preparation failure kept its window and retry produced "
        "a ready frame"
    )


if __name__ == "__main__":
    main()
