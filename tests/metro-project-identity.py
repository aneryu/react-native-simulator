import argparse
import http.server
import json
import pathlib
import subprocess
import tempfile
import threading
import urllib.parse


class MetroHandler(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        path = urllib.parse.urlsplit(self.path).path
        if path == "/status":
            body = b"packager-status:running"
            self.send_response(200)
            self.send_header("Content-Type", "text/plain")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
            return

        if path == "/__rnsim_project_root_probe_8f3d6c4a__.bundle":
            body = json.dumps(
                {
                    "type": "UnableToResolveError",
                    "originModulePath": str(self.server.actual_root) + "/.",
                    "targetModuleName":
                        "./__rnsim_project_root_probe_8f3d6c4a__",
                }
            ).encode()
            self.send_response(404)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
            return

        self.send_response(404)
        self.send_header("Content-Length", "0")
        self.end_headers()

    def log_message(self, format, *args):
        del format, args


def run_doctor(rnsim, project_root, url=None):
    command = [rnsim, "doctor", "--json"]
    if url is not None:
        command.extend(["--url", url])
    completed = subprocess.run(
        command,
        cwd=project_root,
        check=False,
        capture_output=True,
        text=True,
        timeout=8,
    )
    if completed.returncode != 0:
        raise AssertionError(
            f"doctor failed ({completed.returncode}): {completed.stderr}"
        )
    return json.loads(completed.stdout)


def require(condition, message):
    if not condition:
        raise AssertionError(message)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--rnsim", required=True)
    parser.add_argument("--project-root", required=True)
    parser.add_argument("--other-root", required=True)
    args = parser.parse_args()

    project_root = pathlib.Path(args.project_root).resolve(strict=True)
    other_root = pathlib.Path(args.other_root).resolve(strict=True)
    server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), MetroHandler)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    url = (
        f"http://127.0.0.1:{server.server_port}/index.bundle"
        "?platform=android&dev=true&minify=false"
    )

    try:
        server.actual_root = project_root
        matching = run_doctor(args.rnsim, project_root, url)["project"]
        matching_metro = matching["metro"]
        require(matching["readyToLaunch"], matching)
        require(matching["status"] == "compatible-metro-verified", matching)
        require(matching_metro["projectVerified"], matching_metro)
        require(matching_metro["projectVerification"] == "probe-match", matching_metro)
        require(
            pathlib.Path(matching_metro["actualProjectRoot"]) == project_root,
            matching_metro,
        )

        server.actual_root = other_root
        mismatch = run_doctor(args.rnsim, project_root, url)["project"]
        mismatch_metro = mismatch["metro"]
        require(mismatch["readyToLaunch"], mismatch)
        require(mismatch["preflightPassed"], mismatch)
        require(mismatch["status"] == "metro-project-mismatch", mismatch)
        require(not mismatch_metro["projectVerified"], mismatch_metro)
        require(
            mismatch_metro["projectVerification"] == "probe-mismatch",
            mismatch_metro,
        )
        require(str(project_root) in mismatch["nextAction"], mismatch)
        require(str(other_root) in mismatch["nextAction"], mismatch)

        # An explicit doctor URL follows the same precedence as launch --url:
        # it must diagnose Metro rather than silently accepting rnsim.json's
        # otherwise valid local bundle.
        with tempfile.TemporaryDirectory() as directory:
            override_root = pathlib.Path(directory).resolve()
            (override_root / "package.json").write_text(
                json.dumps(
                    {
                        "name": "doctor-url-override",
                        "private": True,
                        "dependencies": {"react-native": "0.87.0"},
                    }
                )
            )
            (override_root / "app.json").write_text(
                json.dumps({"name": "DoctorUrlOverride"})
            )
            (override_root / "offline.jsbundle").write_text("// offline\n")
            (override_root / "rnsim.json").write_text(
                json.dumps(
                    {
                        "schemaVersion": 1,
                        "reactNative": "0.87.0",
                        "bundle": "offline.jsbundle",
                    }
                )
            )
            server.actual_root = override_root
            override = run_doctor(args.rnsim, override_root, url)["project"]
            override_metro = override["metro"]
            require(override["readyToLaunch"], override)
            require(override_metro["required"], override_metro)
            require(override_metro["projectVerified"], override_metro)
            require(
                pathlib.Path(override_metro["actualProjectRoot"])
                == override_root,
                override_metro,
            )

        # A configured bundle is launch's selected source even when it does
        # not exist. Doctor must report that exact failure instead of silently
        # falling back to entry discovery and Metro.
        with tempfile.TemporaryDirectory() as directory:
            missing_root = pathlib.Path(directory).resolve()
            (missing_root / "package.json").write_text(
                json.dumps(
                    {
                        "name": "doctor-missing-bundle",
                        "private": True,
                        "dependencies": {"react-native": "0.87.0"},
                    }
                )
            )
            (missing_root / "app.json").write_text(
                json.dumps({"name": "DoctorMissingBundle"})
            )
            missing_bundle = missing_root / "missing.jsbundle"
            (missing_root / "rnsim.json").write_text(
                json.dumps(
                    {
                        "schemaVersion": 1,
                        "reactNative": "0.87.0",
                        "bundle": missing_bundle.name,
                    }
                )
            )
            missing = run_doctor(args.rnsim, missing_root)["project"]
            require(not missing["readyToLaunch"], missing)
            require(not missing["preflightPassed"], missing)
            require(missing["status"] == "missing-configured-bundle", missing)
            require(not missing["metro"]["required"], missing["metro"])
            require(missing["config"]["bundle"] == str(missing_bundle), missing)
            require(not missing["config"]["bundlePresent"], missing)
            require(str(missing_bundle) in missing["nextAction"], missing)
    finally:
        server.shutdown()
        server.server_close()
        thread.join(timeout=2)

    print("Metro project identity remains diagnostic and non-blocking")


if __name__ == "__main__":
    main()
